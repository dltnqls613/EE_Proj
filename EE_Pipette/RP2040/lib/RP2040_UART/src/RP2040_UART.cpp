#include "RP2040_UART.h"

#include <cstring>

#include "pico/multicore.h"

RP2040_UART* RP2040_UART::instances_[2] = {nullptr, nullptr};
uint8_t RP2040_UART::instance_count_ = 0;
uint8_t RP2040_UART::irq_core_ = 0xffu;

void rp2040_uart_dma_irq0_handler() {
    const uint32_t pending = dma_hw->ints0;

    for (uint8_t i = 0; i < 2; ++i) {
        RP2040_UART* const dev = RP2040_UART::instances_[i];
        if (dev == nullptr || !dev->begun_) {
            continue;
        }

        const uint32_t tx_mask = 1u << dev->dma_tx_;
        if ((pending & tx_mask) != 0u) {
            dma_hw->ints0 = tx_mask;
            dev->handleTxDmaComplete();
        }

        for (uint8_t slot = 0; slot < 2; ++slot) {
            const int channel = dev->dma_rx_[slot];
            const uint32_t rx_mask = 1u << channel;
            if ((pending & rx_mask) != 0u) {
                dma_hw->ints0 = rx_mask;
                dev->handleRxDmaComplete(channel);
            }
        }
    }
}

RP2040_UART::RP2040_UART()
    : uart_(nullptr),
      uart_index_(0xffu),
      tx_pin_(0),
      rx_pin_(0),
      requested_baudrate_(0),
      actual_baudrate_(0),
      dma_tx_(-1),
      dma_rx_{-1, -1},
      tx_write_total_(0),
      tx_read_total_(0),
      tx_dma_len_(0),
      tx_dma_active_(false),
      rx_completed_total_(0),
      rx_active_slot_(0),
      rx_read_total_(0),
      rx_dropped_(0),
      uart_errors_(0),
      tx_stats_base_(0),
      rx_stats_base_(0),
      begun_(false),
      begin_error_(BeginError::None) {}

RP2040_UART::~RP2040_UART() {
    end();
}

bool RP2040_UART::validTxPin(uint8_t uart_index, uint pin) {
    if (pin > 29u) {
        return false;
    }
    const uint mod = pin & 0x0fu;
    return uart_index == 0u ? (mod == 0u || mod == 12u)
                            : (mod == 4u || mod == 8u);
}

bool RP2040_UART::validRxPin(uint8_t uart_index, uint pin) {
    if (pin > 29u) {
        return false;
    }
    const uint mod = pin & 0x0fu;
    return uart_index == 0u ? (mod == 1u || mod == 13u)
                            : (mod == 5u || mod == 9u);
}

bool RP2040_UART::claimDmaChannels() {
    dma_tx_ = dma_claim_unused_channel(false);
    if (dma_tx_ < 0) {
        return false;
    }

    dma_rx_[0] = dma_claim_unused_channel(false);
    if (dma_rx_[0] < 0) {
        dma_channel_unclaim(static_cast<uint>(dma_tx_));
        dma_tx_ = -1;
        return false;
    }

    dma_rx_[1] = dma_claim_unused_channel(false);
    if (dma_rx_[1] < 0) {
        dma_channel_unclaim(static_cast<uint>(dma_rx_[0]));
        dma_channel_unclaim(static_cast<uint>(dma_tx_));
        dma_rx_[0] = -1;
        dma_tx_ = -1;
        return false;
    }

    return true;
}

void RP2040_UART::releaseDmaChannels() {
    // 먼저 세 채널의 IRQ를 모두 차단한다. RX 채널은 서로 chain되어 있으므로
    // 하나를 abort하는 순간 다른 채널이 trigger되어도 IRQ가 새로 들어오지 않는다.
    if (dma_tx_ >= 0) {
        dma_channel_set_irq0_enabled(static_cast<uint>(dma_tx_), false);
    }
    for (uint8_t slot = 0; slot < 2; ++slot) {
        if (dma_rx_[slot] >= 0) {
            dma_channel_set_irq0_enabled(static_cast<uint>(dma_rx_[slot]), false);
        }
    }

    if (dma_tx_ >= 0) {
        dma_channel_abort(static_cast<uint>(dma_tx_));
        dma_hw->ints0 = 1u << dma_tx_;
        dma_channel_unclaim(static_cast<uint>(dma_tx_));
        dma_tx_ = -1;
    }

    for (uint8_t slot = 0; slot < 2; ++slot) {
        if (dma_rx_[slot] >= 0) {
            dma_channel_abort(static_cast<uint>(dma_rx_[slot]));
            dma_hw->ints0 = 1u << dma_rx_[slot];
            dma_channel_unclaim(static_cast<uint>(dma_rx_[slot]));
            dma_rx_[slot] = -1;
        }
    }
}

void RP2040_UART::configureTxDma() {
    dma_channel_config config =
        dma_channel_get_default_config(static_cast<uint>(dma_tx_));
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, uart_get_dreq(uart_, true));
    channel_config_set_high_priority(&config, true);

    dma_channel_configure(
        static_cast<uint>(dma_tx_), &config, &uart_get_hw(uart_)->dr,
        tx_buffer_, 0, false);
}

void RP2040_UART::configureRxDma() {
    for (uint8_t slot = 0; slot < 2; ++slot) {
        const uint channel = static_cast<uint>(dma_rx_[slot]);
        const uint next = static_cast<uint>(dma_rx_[slot ^ 1u]);

        dma_channel_config config = dma_channel_get_default_config(channel);
        channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
        channel_config_set_read_increment(&config, false);
        channel_config_set_write_increment(&config, true);
        channel_config_set_dreq(&config, uart_get_dreq(uart_, false));
        channel_config_set_chain_to(&config, next);
        channel_config_set_high_priority(&config, true);

        dma_channel_configure(
            channel, &config, rx_buffer_, &uart_get_hw(uart_)->dr,
            RP2040_UART_RX_BUFFER_SIZE, false);
    }
}

bool RP2040_UART::begin(uart_inst_t* uart, uint tx_pin, uint rx_pin,
                        uint32_t baudrate) {
    begin_error_ = BeginError::None;

    if (begun_) {
        begin_error_ = BeginError::AlreadyBegun;
        return false;
    }
    if (uart != uart0 && uart != uart1) {
        begin_error_ = BeginError::InvalidUart;
        return false;
    }

    const uint8_t index = static_cast<uint8_t>(uart_get_index(uart));
    if (!validTxPin(index, tx_pin) || !validRxPin(index, rx_pin)) {
        begin_error_ = BeginError::InvalidPin;
        return false;
    }
    if (instances_[index] != nullptr) {
        begin_error_ = BeginError::UartInUse;
        return false;
    }
    if (instance_count_ != 0u && irq_core_ != get_core_num()) {
        // DMA_IRQ_0 shared handler는 설치한 코어의 벡터 테이블에 속한다.
        begin_error_ = BeginError::UartInUse;
        return false;
    }
    if (!claimDmaChannels()) {
        begin_error_ = BeginError::NoDmaChannel;
        return false;
    }

    uart_ = uart;
    uart_index_ = index;
    tx_pin_ = tx_pin;
    rx_pin_ = rx_pin;
    requested_baudrate_ = baudrate;

    tx_write_total_ = 0;
    tx_read_total_ = 0;
    tx_dma_len_ = 0;
    tx_dma_active_ = false;
    rx_completed_total_ = 0;
    rx_active_slot_ = 0;
    rx_read_total_ = 0;
    rx_dropped_ = 0;
    uart_errors_ = 0;
    tx_stats_base_ = 0;
    rx_stats_base_ = 0;

    actual_baudrate_ = uart_init(uart_, requested_baudrate_);
    gpio_set_function(tx_pin_, GPIO_FUNC_UART);
    gpio_set_function(rx_pin_, GPIO_FUNC_UART);
    uart_set_hw_flow(uart_, false, false);
    uart_set_format(uart_, 8, 1, UART_PARITY_NONE);

    // FIFO를 끄면 RX/TX DREQ가 바이트마다 발생한다. 1.5 Mbps에서 DMA가
    // 처리하기에는 충분히 느리며, 짧은 패킷이 FIFO 임계값 아래에 남는
    // 지연을 없앤다.
    uart_set_fifo_enabled(uart_, false);

    configureTxDma();
    configureRxDma();

    instances_[uart_index_] = this;
    if (instance_count_ == 0u) {
        irq_core_ = static_cast<uint8_t>(get_core_num());
        irq_add_shared_handler(DMA_IRQ_0, rp2040_uart_dma_irq0_handler,
                               PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
        irq_set_enabled(DMA_IRQ_0, true);
    }
    ++instance_count_;

    dma_hw->ints0 = (1u << dma_tx_) |
                    (1u << dma_rx_[0]) |
                    (1u << dma_rx_[1]);
    dma_channel_set_irq0_enabled(static_cast<uint>(dma_tx_), true);
    dma_channel_set_irq0_enabled(static_cast<uint>(dma_rx_[0]), true);
    dma_channel_set_irq0_enabled(static_cast<uint>(dma_rx_[1]), true);

    begun_ = true;

    // RX A가 끝나면 B, B가 끝나면 A가 하드웨어 체인으로 즉시 시작된다.
    dma_start_channel_mask(1u << dma_rx_[0]);
    return true;
}

void RP2040_UART::end() {
    if (!begun_) {
        return;
    }

    const uint32_t irq_state = save_and_disable_interrupts();
    begun_ = false;
    instances_[uart_index_] = nullptr;

    // 새로운 UART DREQ가 발생하지 않도록 한 뒤 DMA를 정리한다.
    uart_deinit(uart_);
    releaseDmaChannels();

    if (instance_count_ > 0u) {
        --instance_count_;
    }
    if (instance_count_ == 0u) {
        irq_remove_handler(DMA_IRQ_0, rp2040_uart_dma_irq0_handler);
        irq_core_ = 0xffu;
    }

    gpio_set_function(tx_pin_, GPIO_FUNC_SIO);
    gpio_set_function(rx_pin_, GPIO_FUNC_SIO);
    uart_ = nullptr;
    uart_index_ = 0xffu;
    actual_baudrate_ = 0;
    restore_interrupts(irq_state);
}

const char* RP2040_UART::lastBeginErrorString() const {
    switch (begin_error_) {
        case BeginError::None:         return "none";
        case BeginError::AlreadyBegun: return "already begun";
        case BeginError::InvalidUart:  return "invalid UART instance";
        case BeginError::InvalidPin:   return "invalid TX/RX pin for UART";
        case BeginError::UartInUse:    return "UART or DMA IRQ core already in use";
        case BeginError::NoDmaChannel: return "not enough DMA channels";
        default:                       return "unknown";
    }
}

void RP2040_UART::startTxLocked() {
    if (!begun_ || tx_dma_active_) {
        return;
    }

    const uint32_t queued = tx_write_total_ - tx_read_total_;
    if (queued == 0u) {
        return;
    }

    const uint32_t read_index = tx_read_total_ & TX_MASK;
    const uint32_t contiguous = RP2040_UART_TX_BUFFER_SIZE - read_index;
    const uint32_t transfer = queued < contiguous ? queued : contiguous;

    tx_dma_len_ = transfer;
    tx_dma_active_ = true;
    __compiler_memory_barrier();

    dma_channel_set_read_addr(static_cast<uint>(dma_tx_),
                              tx_buffer_ + read_index, false);
    dma_channel_set_trans_count(static_cast<uint>(dma_tx_), transfer, true);
}

void RP2040_UART::handleTxDmaComplete() {
    tx_read_total_ += tx_dma_len_;
    tx_dma_len_ = 0;
    tx_dma_active_ = false;
    __compiler_memory_barrier();
    startTxLocked();
}

void RP2040_UART::handleRxDmaComplete(int channel) {
    const uint8_t completed_slot =
        channel == dma_rx_[0] ? 0u : 1u;
    const uint8_t next_slot = completed_slot ^ 1u;

    rx_completed_total_ += RP2040_UART_RX_BUFFER_SIZE;
    rx_active_slot_ = next_slot;
    __compiler_memory_barrier();

    // 반대 채널이 수신하는 동안 끝난 채널을 다음 lap용으로 준비한다.
    dma_channel_set_write_addr(static_cast<uint>(channel), rx_buffer_, false);
    dma_channel_set_trans_count(static_cast<uint>(channel),
                                RP2040_UART_RX_BUFFER_SIZE, false);

    const uint next_channel = static_cast<uint>(dma_rx_[next_slot]);
    if (!dma_channel_is_busy(next_channel) &&
        dma_channel_hw_addr(next_channel)->transfer_count != 0u) {
        // IRQ가 한 lap 이상 막혀 체인 trigger가 유실된 경우의 복구 경로다.
        dma_start_channel_mask(1u << next_channel);
    }
}

uint32_t RP2040_UART::sampleRxProduced() const {
    if (!begun_) {
        return rx_read_total_;
    }

    for (;;) {
        const uint32_t completed_before = rx_completed_total_;
        const uint8_t slot_before = rx_active_slot_;
        __compiler_memory_barrier();

        const uint channel = static_cast<uint>(dma_rx_[slot_before]);
        const uintptr_t address = dma_channel_hw_addr(channel)->write_addr;

        __compiler_memory_barrier();
        const uint32_t completed_after = rx_completed_total_;
        const uint8_t slot_after = rx_active_slot_;

        if (completed_before == completed_after && slot_before == slot_after) {
            const uintptr_t base = reinterpret_cast<uintptr_t>(rx_buffer_);
            uint32_t offset = 0;
            if (address > base) {
                const uintptr_t raw_offset = address - base;
                offset = raw_offset > RP2040_UART_RX_BUFFER_SIZE
                             ? RP2040_UART_RX_BUFFER_SIZE
                             : static_cast<uint32_t>(raw_offset);
            }
            return completed_before + offset;
        }
    }
}

uint32_t RP2040_UART::syncRxReader(uint32_t produced) {
    const uint32_t capacity =
        RP2040_UART_RX_BUFFER_SIZE - RP2040_UART_RX_GUARD_SIZE;
    uint32_t pending = produced - rx_read_total_;

    if (pending > capacity) {
        const uint32_t dropped = pending - capacity;
        rx_read_total_ += dropped;
        rx_dropped_ += dropped;
        pending = capacity;
    }
    return pending;
}

void RP2040_UART::pollUartErrors() {
    if (!begun_) {
        return;
    }
    uart_hw_t* const hw = uart_get_hw(uart_);
    const uint32_t errors = hw->rsr & 0x0fu;
    if (errors != 0u) {
        ++uart_errors_;
        hw->rsr = 0u;
    }
}

int RP2040_UART::available() {
    if (!begun_) {
        return 0;
    }
    pollUartErrors();
    return static_cast<int>(syncRxReader(sampleRxProduced()));
}

size_t RP2040_UART::read(uint8_t* data, size_t max_len) {
    if (!begun_ || data == nullptr || max_len == 0u) {
        return 0;
    }

    const uint32_t produced = sampleRxProduced();
    const uint32_t pending = syncRxReader(produced);
    const uint32_t count =
        max_len < pending ? static_cast<uint32_t>(max_len) : pending;
    if (count == 0u) {
        return 0;
    }

    const uint32_t read_index = rx_read_total_ & RX_MASK;
    const uint32_t first =
        count < (RP2040_UART_RX_BUFFER_SIZE - read_index)
            ? count
            : (RP2040_UART_RX_BUFFER_SIZE - read_index);

    memcpy(data, rx_buffer_ + read_index, first);
    if (first < count) {
        memcpy(data + first, rx_buffer_, count - first);
    }

    __compiler_memory_barrier();
    rx_read_total_ += count;
    return count;
}

int RP2040_UART::read() {
    uint8_t data = 0;
    return read(&data, 1u) == 1u ? static_cast<int>(data) : -1;
}

int RP2040_UART::peek() {
    if (!begun_) {
        return -1;
    }
    const uint32_t pending = syncRxReader(sampleRxProduced());
    if (pending == 0u) {
        return -1;
    }
    return rx_buffer_[rx_read_total_ & RX_MASK];
}

void RP2040_UART::clearRx() {
    if (!begun_) {
        return;
    }
    rx_read_total_ = sampleRxProduced();
}

size_t RP2040_UART::tryWrite(const uint8_t* data, size_t len) {
    if (!begun_ || data == nullptr || len == 0u) {
        return 0;
    }

    const uint32_t used = tx_write_total_ - tx_read_total_;
    const uint32_t space = RP2040_UART_TX_BUFFER_SIZE - used;
    const uint32_t count = len < space ? static_cast<uint32_t>(len) : space;
    if (count == 0u) {
        return 0;
    }

    const uint32_t write_index = tx_write_total_ & TX_MASK;
    const uint32_t first =
        count < (RP2040_UART_TX_BUFFER_SIZE - write_index)
            ? count
            : (RP2040_UART_TX_BUFFER_SIZE - write_index);

    memcpy(tx_buffer_ + write_index, data, first);
    if (first < count) {
        memcpy(tx_buffer_, data + first, count - first);
    }

    const uint32_t irq_state = save_and_disable_interrupts();
    __compiler_memory_barrier();
    tx_write_total_ += count;
    startTxLocked();
    restore_interrupts(irq_state);
    return count;
}

size_t RP2040_UART::write(const uint8_t* data, size_t len) {
    if (!begun_ || data == nullptr || len == 0u) {
        return 0;
    }

    size_t written = 0;
    while (written < len) {
        const size_t n = tryWrite(data + written, len - written);
        written += n;
        if (n == 0u) {
            tight_loop_contents();
        }
    }
    return written;
}

size_t RP2040_UART::write(uint8_t data) {
    return write(&data, 1u);
}

size_t RP2040_UART::txSpace() const {
    if (!begun_) {
        return 0;
    }
    const uint32_t used = tx_write_total_ - tx_read_total_;
    return RP2040_UART_TX_BUFFER_SIZE - used;
}

int RP2040_UART::availableForWrite() {
    return static_cast<int>(txSpace());
}

bool RP2040_UART::isTxBusy() const {
    return begun_ && (tx_dma_active_ || tx_write_total_ != tx_read_total_);
}

void RP2040_UART::flush() {
    if (!begun_) {
        return;
    }
    while (tx_write_total_ != tx_read_total_ || tx_dma_active_) {
        tight_loop_contents();
    }
    uart_tx_wait_blocking(uart_);
}

RP2040_UART::Stats RP2040_UART::getStats() {
    Stats stats{};
    if (!begun_) {
        return stats;
    }
    pollUartErrors();
    const uint32_t produced = sampleRxProduced();
    syncRxReader(produced);
    stats.tx_bytes = tx_read_total_ - tx_stats_base_;
    stats.rx_bytes = produced - rx_stats_base_;
    stats.rx_dropped = rx_dropped_;
    stats.uart_errors = uart_errors_;
    return stats;
}

void RP2040_UART::resetStats() {
    if (!begun_) {
        return;
    }
    const uint32_t irq_state = save_and_disable_interrupts();
    tx_stats_base_ = tx_read_total_;
    rx_stats_base_ = sampleRxProduced();
    rx_dropped_ = 0;
    uart_errors_ = 0;
    restore_interrupts(irq_state);
}

uint32_t RP2040_UART::setBaudrate(uint32_t baudrate) {
    if (!begun_ || baudrate == 0u) {
        return 0;
    }
    requested_baudrate_ = baudrate;
    actual_baudrate_ = uart_set_baudrate(uart_, baudrate);
    return actual_baudrate_;
}

void RP2040_UART::setFormat(uint data_bits, uint stop_bits,
                            uart_parity_t parity) {
    if (begun_) {
        uart_set_format(uart_, data_bits, stop_bits, parity);
    }
}

void RP2040_UART::setFlowControl(bool cts, bool rts) {
    if (begun_) {
        uart_set_hw_flow(uart_, cts, rts);
    }
}
