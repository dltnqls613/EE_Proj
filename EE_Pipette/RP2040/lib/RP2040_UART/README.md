# RP2040_UART

Arduino-Pico(Earle Philhower) 환경의 RP2040 UART0/UART1용 전이중 DMA
라이브러리다.

## 구조

- TX: 4 KiB 링버퍼에서 UART DR로 직접 DMA 전송
- RX: 두 DMA 채널이 4 KiB 최종 링버퍼를 번갈아 채우는 ping-pong 구조
- RX 경로에는 256바이트 청크 버퍼나 IRQ 내부 memcpy가 없다.
- UART0과 UART1은 하나의 공유 `DMA_IRQ_0` 핸들러를 사용한다.
- 각 UART는 TX 1개 + RX 2개, 총 3개 DMA 채널을 사용한다.
- UART0과 UART1을 모두 쓰면 RP2040의 12개 DMA 채널 중 6개를 사용한다.

## 사용

```cpp
#include <RP2040_UART.h>

RP2040_UART A_Uart;
RP2040_UART B_Uart;

void setup() {
    A_Uart.begin(uart0, 12, 13, 1500000);
    B_Uart.begin(uart1, 4, 5, 1500000);
}
```

`begin()`, `available()`, `read()`, `write()`, `flush()`는 모두 같은
애플리케이션 코어에서 호출해야 한다. 현재 SBActuator 프로젝트에서는 코어0이
UART와 명령 파싱을 담당하고 코어1이 스텝 펄스를 생성하므로 이 조건을 만족한다.

`write()`는 전체 데이터가 TX 링버퍼에 들어갈 때까지 기다려 패킷 일부만
전송되는 일을 막는다. 제어 루프에서 절대로 기다리면 안 되는 경로는
`tryWrite()`를 사용하고 반환 길이를 검사한다.

## 실제 장치 검증 항목

1. UART0 TX-RX, UART1 TX-RX를 각각 점퍼로 연결한다.
2. 두 UART에 동시에 PRBS 또는 증가 카운터 데이터를 보낸다.
3. 1.5 Mbps에서 최소 30분 동안 CRC, 순서, 길이를 검사한다.
4. `getStats()`의 `rx_dropped`와 `uart_errors`가 0인지 확인한다.
5. 기존 SerialUART와 같은 명령 부하에서 `Manager()` 최대 실행 간격을 비교한다.

버퍼 크기는 `RP2040_UART_TX_BUFFER_SIZE`,
`RP2040_UART_RX_BUFFER_SIZE` 빌드 매크로로 변경할 수 있으며 반드시 2의
거듭제곱이어야 한다.
