#ifndef SB_BQ_H
#define SB_BQ_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// RP2040 하드웨어 직접 접근
#include "pico/stdlib.h"
#include "hardware/sync.h"

class SB_BQ {
public:
    SB_BQ() 
        : _buf(nullptr), _rIdx(0), _wIdx(0), 
          _len(0), _cnt(0), _mask(0), _overwrite(false) {}
    
    ~SB_BQ() {
        if (_buf) free(_buf);
    }
    
    // 초기화 (2의 제곱 권장, 아니면 자동 올림)
    inline bool init(size_t size, bool overwrite = false) __attribute__((always_inline)) {
        if (size == 0) return false;
        
        // 2의 거듭제곱이 아니면 다음 2의 거듭제곱으로 올림
        size_t actualSize = size;
        if ((size & (size - 1)) != 0) {
            // 2의 거듭제곱으로 올림
            actualSize = 1;
            while (actualSize < size) {
                actualSize <<= 1;
            }
        }
        
        if (_buf) free(_buf);
        
        _buf = (uint8_t *)malloc(actualSize);
        if (!_buf) return false;
        
        _rIdx = 0;
        _wIdx = 0;
        _len = actualSize;
        _mask = actualSize - 1;
        _cnt = 0;
        _overwrite = overwrite;
        
        return true;
    }
    
    // 단일 바이트 쓰기 (인터럽트 안전)
    inline bool push(uint8_t data) __attribute__((always_inline)) {
        uint32_t status = save_and_disable_interrupts();
        if (_cnt >= _len) {
            if (_overwrite) {
                // 읽기 포인터만 이동 (오래된 데이터 건너뜀)
                _rIdx = (_rIdx + 1) & _mask;
                // _cnt는 그대로 (_len 유지)
            }
            else
            {
                restore_interrupts(status);
                return false;
            }
        } else {
            _cnt++;  // 가득 차지 않았을 때만 증가
        }

        // 데이터 쓰기
        _buf[_wIdx] = data;
        _wIdx = (_wIdx + 1) & _mask;
        
        // _buf[_wIdx] = data;
        // _wIdx = (_wIdx + 1) & _mask;
        // _cnt++;
        
        restore_interrupts(status);
        return true;
    }
    
    // 단일 바이트 쓰기 (인터럽트 비활성화 없음 - 더 빠름)
    inline bool pushFast(uint8_t data) __attribute__((always_inline)) {
        if (_cnt >= _len) {
            if (_overwrite) {
                // 읽기 포인터만 이동 (오래된 데이터 건너뜀)
                _rIdx = (_rIdx + 1) & _mask;
                // _cnt는 그대로 (_len 유지)
            }
            else
            {
                return false;
            }
        } else {
            _cnt++;  // 가득 차지 않았을 때만 증가
        }

        // 데이터 쓰기
        _buf[_wIdx] = data;
        _wIdx = (_wIdx + 1) & _mask;
        __compiler_memory_barrier();

        
        if (_cnt >= _len) return false;

        
        // _buf[_wIdx] = data;
        // _wIdx = (_wIdx + 1) & _mask;
        // __compiler_memory_barrier();  // 컴파일러 최적화 방지
        // _cnt++;
        
        return true;
    }
    
    // 다중 바이트 쓰기 (memcpy 최적화)
    inline bool write(const uint8_t *data, size_t len) __attribute__((always_inline)) {
        uint32_t status = save_and_disable_interrupts();
        
        if (_cnt + len > _len) {
            if (_overwrite) {
                // 덮어쓰기 모드: 필요한 만큼 오래된 데이터 제거
                size_t overflow = (_cnt + len) - _len;
                _rIdx = _mask ? ((_rIdx + overflow) & _mask) : ((_rIdx + overflow) % _len);
                _cnt -= overflow;
            } else {
                restore_interrupts(status);
                return false;
            }
        }
        
        size_t chunk1 = (_len - _wIdx < len) ? (_len - _wIdx) : len;
        memcpy(_buf + _wIdx, data, chunk1);
        
        if (chunk1 < len) {
            memcpy(_buf, data + chunk1, len - chunk1);
        }
        
        _wIdx = _mask ? ((_wIdx + len) & _mask) : ((_wIdx + len) % _len);
        _cnt += len;
        
        restore_interrupts(status);
        return true;
    }
    
    // 다중 바이트 쓰기 (인터럽트 비활성화 없음)
    inline bool writeFast(const uint8_t *data, size_t len) __attribute__((always_inline)) {
        if (_cnt + len > _len) {
            if (_overwrite) {
                // 덮어쓰기 모드: 필요한 만큼 오래된 데이터 제거
                size_t overflow = (_cnt + len) - _len;
                _rIdx = _mask ? ((_rIdx + overflow) & _mask) : ((_rIdx + overflow) % _len);
                _cnt -= overflow;
            } else {
                return false;
            }
        }
        
        size_t chunk1 = (_len - _wIdx < len) ? (_len - _wIdx) : len;
        memcpy(_buf + _wIdx, data, chunk1);
        
        if (chunk1 < len) {
            memcpy(_buf, data + chunk1, len - chunk1);
        }
        
        _wIdx = _mask ? ((_wIdx + len) & _mask) : ((_wIdx + len) % _len);
        __compiler_memory_barrier();
        _cnt += len;
        
        return true;
    }
    
    // 단일 바이트 읽기
    inline int pop() __attribute__((always_inline)) {
        uint32_t status = save_and_disable_interrupts();
        
        if (_cnt == 0) {
            restore_interrupts(status);
            return -1;
        }
        
        uint8_t data = _buf[_rIdx];
        _rIdx = _mask ? ((_rIdx + 1) & _mask) : ((_rIdx + 1) % _len);
        _cnt--;
        
        restore_interrupts(status);
        return data;
    }
    
    // 단일 바이트 읽기 (인터럽트 비활성화 없음)
    inline int popFast() __attribute__((always_inline)) {
        if (_cnt == 0) return -1;
        
        uint8_t data = _buf[_rIdx];
        _rIdx = _mask ? ((_rIdx + 1) & _mask) : ((_rIdx + 1) % _len);
        __compiler_memory_barrier();
        _cnt--;
        
        return data;
    }
    
    // 다중 바이트 읽기
    inline bool read(uint8_t *data, size_t len) __attribute__((always_inline)) {
        uint32_t status = save_and_disable_interrupts();
        
        if (_cnt < len) {
            restore_interrupts(status);
            return false;
        }
        
        size_t chunk1 = (_len - _rIdx < len) ? (_len - _rIdx) : len;
        memcpy(data, _buf + _rIdx, chunk1);
        
        if (chunk1 < len) {
            memcpy(data + chunk1, _buf, len - chunk1);
        }
        
        _rIdx = _mask ? ((_rIdx + len) & _mask) : ((_rIdx + len) % _len);
        _cnt -= len;
        
        restore_interrupts(status);
        return true;
    }
    
    // 다중 바이트 읽기 (인터럽트 비활성화 없음)
    inline bool readFast(uint8_t *data, size_t len) __attribute__((always_inline)) {
        if (_cnt < len) return false;
        
        size_t chunk1 = (_len - _rIdx < len) ? (_len - _rIdx) : len;
        memcpy(data, _buf + _rIdx, chunk1);
        
        if (chunk1 < len) {
            memcpy(data + chunk1, _buf, len - chunk1);
        }
        
        _rIdx = _mask ? ((_rIdx + len) & _mask) : ((_rIdx + len) % _len);
        __compiler_memory_barrier();
        _cnt -= len;
        
        return true;
    }

    inline bool drop(size_t n)
    {
        if (n==0) return true;
        if (!_buf) return false;
        if (int(_cnt-n) < 0) return false;

        __asm__ __volatile__("" ::: "memory");
        _rIdx = (_rIdx + n) & _mask;
        _cnt-=n;
        return true;
    }
    
    // Peek (읽지 않고 확인)
    inline int peek() const __attribute__((always_inline)) {
        return (_cnt == 0) ? -1 : _buf[_rIdx];
    }
    
    // 상태 확인
    inline size_t avail() const __attribute__((always_inline)) {
        return _cnt;
    }
    
    inline size_t space() const __attribute__((always_inline)) {
        return _len - _cnt;
    }
    
    inline bool empty() const __attribute__((always_inline)) {
        return (_cnt == 0);
    }
    
    inline bool full() const __attribute__((always_inline)) {
        return (_cnt >= _len);
    }
    
    inline size_t size() const __attribute__((always_inline)) {
        return _len;
    }
    
    inline void clear() __attribute__((always_inline)) {
        uint32_t status = save_and_disable_interrupts();
        _rIdx = 0;
        _wIdx = 0;
        _cnt = 0;
        restore_interrupts(status);
    }
    
    inline void clearFast() __attribute__((always_inline)) {
        _rIdx = 0;
        _wIdx = 0;
        _cnt = 0;
    }
    
    // 덮어쓰기 모드 설정/확인
    inline void setOverwrite(bool enable) __attribute__((always_inline)) {
        _overwrite = enable;
    }
    
    inline bool isOverwriteMode() const __attribute__((always_inline)) {
        return _overwrite;
    }

    inline int operator[](size_t index) const __attribute__((always_inline)) {
        if (!_buf || index >= _cnt) return -1;

        uint32_t status = save_and_disable_interrupts();

        // ring buffer 논리 인덱싱 (2^n 보장)
        size_t pos = (_rIdx + index) & _mask;
        uint8_t val = _buf[pos];

        restore_interrupts(status);
        return (int)val;
    }

private:
    uint8_t *_buf;
    volatile size_t _rIdx;
    volatile size_t _wIdx;
    size_t _len;
    volatile size_t _cnt;
    size_t _mask;
    bool _overwrite;  // 덮어쓰기 모드
};

#endif