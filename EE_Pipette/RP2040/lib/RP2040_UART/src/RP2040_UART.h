#ifndef RP2040_UART_H
#define RP2040_UART_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#ifndef RP2040_UART_TX_BUFFER_SIZE
#define RP2040_UART_TX_BUFFER_SIZE 4096u
#endif

#ifndef RP2040_UART_RX_BUFFER_SIZE
#define RP2040_UART_RX_BUFFER_SIZE 4096u
#endif

// DMA가 읽지 않은 가장 오래된 바이트를 덮어쓰는 동안 read()가 복사하는
// 상황을 피하기 위한 여유 공간이다. 64바이트는 1.5 Mbps에서 약 427 us다.
#ifndef RP2040_UART_RX_GUARD_SIZE
#define RP2040_UART_RX_GUARD_SIZE 64u
#endif

class RP2040_UART final : public Stream {
public:
    enum class BeginError : uint8_t {
        None = 0,
        AlreadyBegun,
        InvalidUart,
        InvalidPin,
        UartInUse,
        NoDmaChannel
    };

    struct Stats {
        uint32_t tx_bytes;
        uint32_t rx_bytes;
        uint32_t rx_dropped;
        uint32_t uart_errors;
    };

    RP2040_UART();
    ~RP2040_UART();

    RP2040_UART(const RP2040_UART&) = delete;
    RP2040_UART& operator=(const RP2040_UART&) = delete;

    /**
     * UART와 DMA를 시작한다.
     *
     * 한 RP2040_UART 객체는 uart0 또는 uart1 하나를 독점한다. begin()과
     * 모든 Stream API는 setup()/loop()가 실행되는 같은 코어에서 호출한다.
     * DMA IRQ는 begin()을 호출한 코어에 설치된다.
     */
    bool begin(uart_inst_t* uart, uint tx_pin, uint rx_pin,
               uint32_t baudrate = 115200u);
    void end();

    bool isBegun() const { return begun_; }
    explicit operator bool() const { return begun_; }
    BeginError lastBeginError() const { return begin_error_; }
    const char* lastBeginErrorString() const;

    // Arduino Stream 호환 API
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;
    size_t write(uint8_t data) override;
    size_t write(const uint8_t* data, size_t len) override;
    using Print::write;

    // 현재 프로젝트의 SerialUART.read(buffer, len) 호출과 호환된다.
    size_t read(uint8_t* data, size_t max_len);

    /**
     * TX 링버퍼에 즉시 들어가는 데이터만 기록한다. 절대 기다리지 않는다.
     * 반환값이 len보다 작으면 호출자가 나머지를 다시 보내야 한다.
     */
    size_t tryWrite(const uint8_t* data, size_t len);

    int availableForWrite();
    size_t txSpace() const;
    bool isTxBusy() const;

    void clearRx();
    size_t rxCapacity() const {
        return RP2040_UART_RX_BUFFER_SIZE - RP2040_UART_RX_GUARD_SIZE;
    }

    Stats getStats();
    void resetStats();

    uint32_t setBaudrate(uint32_t baudrate);
    uint32_t baudrate() const { return actual_baudrate_; }
    void setFormat(uint data_bits, uint stop_bits, uart_parity_t parity);
    void setFlowControl(bool cts, bool rts);

private:
    static_assert((RP2040_UART_TX_BUFFER_SIZE &
                   (RP2040_UART_TX_BUFFER_SIZE - 1u)) == 0u,
                  "RP2040_UART_TX_BUFFER_SIZE must be a power of two");
    static_assert((RP2040_UART_RX_BUFFER_SIZE &
                   (RP2040_UART_RX_BUFFER_SIZE - 1u)) == 0u,
                  "RP2040_UART_RX_BUFFER_SIZE must be a power of two");
    static_assert(RP2040_UART_RX_GUARD_SIZE < RP2040_UART_RX_BUFFER_SIZE,
                  "RP2040_UART_RX_GUARD_SIZE must be smaller than RX buffer");

    static constexpr uint32_t TX_MASK = RP2040_UART_TX_BUFFER_SIZE - 1u;
    static constexpr uint32_t RX_MASK = RP2040_UART_RX_BUFFER_SIZE - 1u;

    static RP2040_UART* instances_[2];
    static uint8_t instance_count_;
    static uint8_t irq_core_;

    uart_inst_t* uart_;
    uint8_t uart_index_;
    uint tx_pin_;
    uint rx_pin_;
    uint32_t requested_baudrate_;
    uint32_t actual_baudrate_;

    int dma_tx_;
    int dma_rx_[2];

    alignas(4) uint8_t tx_buffer_[RP2040_UART_TX_BUFFER_SIZE];
    alignas(4) uint8_t rx_buffer_[RP2040_UART_RX_BUFFER_SIZE];

    // 32비트 누적 카운터는 자연스러운 unsigned wrap을 이용한다.
    // 버퍼 사용량은 항상 2^31보다 작으므로 약 8시간마다 wrap되어도 안전하다.
    volatile uint32_t tx_write_total_;
    volatile uint32_t tx_read_total_;
    volatile uint32_t tx_dma_len_;
    volatile bool tx_dma_active_;

    volatile uint32_t rx_completed_total_;
    volatile uint8_t rx_active_slot_;
    uint32_t rx_read_total_;
    volatile uint32_t rx_dropped_;
    volatile uint32_t uart_errors_;

    uint32_t tx_stats_base_;
    uint32_t rx_stats_base_;

    volatile bool begun_;
    BeginError begin_error_;

    bool claimDmaChannels();
    void releaseDmaChannels();
    void configureTxDma();
    void configureRxDma();

    void startTxLocked();
    void handleTxDmaComplete();
    void handleRxDmaComplete(int channel);

    uint32_t sampleRxProduced() const;
    uint32_t syncRxReader(uint32_t produced);
    void pollUartErrors();

    static bool validTxPin(uint8_t uart_index, uint pin);
    static bool validRxPin(uint8_t uart_index, uint pin);

    friend void rp2040_uart_dma_irq0_handler();
};

#endif
