#ifndef SB_FQ_H
#define SB_FQ_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// RP2040 하드웨어 직접 접근
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <cmath>

class SB_FQ {
public:
    SB_FQ() 
        : _buf(nullptr), _rIdx(0), _wIdx(0), 
          _len(0), _cnt(0), _mask(0), _overwrite(false) {}
    
    ~SB_FQ() {
        if (_buf) free(_buf);
    }
    
    // 초기화 (2의 제곱 권장, 아니면 자동 올림)
    inline bool init(size_t size, bool overwrite = false) __attribute__((always_inline)) {
        if (size == 0) return false;
        
        // 2의 거듭제곱이 아니면 다음 2의 거듭제곱으로 올림
        size_t actualSize = size;
        if ((size & (size - 1)) != 0) {
            actualSize = 1;
            while (actualSize < size) {
                actualSize <<= 1;
            }
        }
        
        if (_buf) free(_buf);
        
        _buf = (float *)malloc(actualSize * sizeof(float));
        if (!_buf) return false;
        
        _rIdx = 0;
        _wIdx = 0;
        _len = actualSize;
        _mask = actualSize - 1;
        _cnt = 0;
        _overwrite = overwrite;
        
        return true;
    }
    
    // 단일 float 쓰기 (인터럽트 안전)
    inline bool push(float data) __attribute__((always_inline)) {
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
        
        restore_interrupts(status);
        return true;
    }
    
    // 단일 float 쓰기 (인터럽트 비활성화 없음 - 더 빠름)
    inline bool pushFast(float data) __attribute__((always_inline)) {
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
        
        return true;
    }
    
    // 다중 float 쓰기 (memcpy 최적화)
    inline bool write(const float *data, size_t len) __attribute__((always_inline)) {
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
        memcpy(_buf + _wIdx, data, chunk1 * sizeof(float));
        
        if (chunk1 < len) {
            memcpy(_buf, data + chunk1, (len - chunk1) * sizeof(float));
        }
        
        _wIdx = _mask ? ((_wIdx + len) & _mask) : ((_wIdx + len) % _len);
        _cnt += len;
        
        restore_interrupts(status);
        return true;
    }
    
    // 다중 float 쓰기 (인터럽트 비활성화 없음)
    inline bool writeFast(const float *data, size_t len) __attribute__((always_inline)) {
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
        memcpy(_buf + _wIdx, data, chunk1 * sizeof(float));
        
        if (chunk1 < len) {
            memcpy(_buf, data + chunk1, (len - chunk1) * sizeof(float));
        }
        
        _wIdx = _mask ? ((_wIdx + len) & _mask) : ((_wIdx + len) % _len);
        __compiler_memory_barrier();
        _cnt += len;
        
        return true;
    }
    
    // 단일 float 읽기 (인터럽트 안전)
    // 실패 시 NAN 반환 → isnan()으로 확인 가능
    inline float pop() __attribute__((always_inline)) {
        uint32_t status = save_and_disable_interrupts();
        
        if (_cnt == 0) {
            restore_interrupts(status);
            return NAN;
        }
        
        float data = _buf[_rIdx];
        _rIdx = _mask ? ((_rIdx + 1) & _mask) : ((_rIdx + 1) % _len);
        _cnt--;
        
        restore_interrupts(status);
        return data;
    }
    
    // 단일 float 읽기 (인터럽트 비활성화 없음)
    inline float popFast() __attribute__((always_inline)) {
        if (_cnt == 0) return NAN;
        
        float data = _buf[_rIdx];
        _rIdx = _mask ? ((_rIdx + 1) & _mask) : ((_rIdx + 1) % _len);
        __compiler_memory_barrier();
        _cnt--;
        
        return data;
    }
    
    // 다중 float 읽기
    inline bool read(float *data, size_t len) __attribute__((always_inline)) {
        uint32_t status = save_and_disable_interrupts();
        
        if (_cnt < len) {
            restore_interrupts(status);
            return false;
        }
        
        size_t chunk1 = (_len - _rIdx < len) ? (_len - _rIdx) : len;
        memcpy(data, _buf + _rIdx, chunk1 * sizeof(float));
        
        if (chunk1 < len) {
            memcpy(data + chunk1, _buf, (len - chunk1) * sizeof(float));
        }
        
        _rIdx = _mask ? ((_rIdx + len) & _mask) : ((_rIdx + len) % _len);
        _cnt -= len;
        
        restore_interrupts(status);
        return true;
    }
    
    // 다중 float 읽기 (인터럽트 비활성화 없음)
    inline bool readFast(float *data, size_t len) __attribute__((always_inline)) {
        if (_cnt < len) return false;
        
        size_t chunk1 = (_len - _rIdx < len) ? (_len - _rIdx) : len;
        memcpy(data, _buf + _rIdx, chunk1 * sizeof(float));
        
        if (chunk1 < len) {
            memcpy(data + chunk1, _buf, (len - chunk1) * sizeof(float));
        }
        
        _rIdx = _mask ? ((_rIdx + len) & _mask) : ((_rIdx + len) % _len);
        __compiler_memory_barrier();
        _cnt -= len;
        
        return true;
    }
    
    // Peek (읽지 않고 확인) - 비어 있으면 NAN 반환
    inline float peek() const __attribute__((always_inline)) {
        return (_cnt == 0) ? NAN : _buf[_rIdx];
    }

    // 비어 있는지 여부를 bool로 확인하는 peek
    inline bool peekTo(float &out) const __attribute__((always_inline)) {
        if (_cnt == 0) return false;
        out = _buf[_rIdx];
        return true;
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

    // 인덱스 접근 - 실패 시 NAN 반환
    inline float operator[](size_t index) const __attribute__((always_inline)) {
        if (!_buf || index >= _cnt) return NAN;

        uint32_t status = save_and_disable_interrupts();

        // ring buffer 논리 인덱싱 (2^n 보장)
        size_t pos = (_rIdx + index) & _mask;
        float val = _buf[pos];

        restore_interrupts(status);
        return val;
    }

private:
    float *_buf;
    volatile size_t _rIdx;
    volatile size_t _wIdx;
    size_t _len;
    volatile size_t _cnt;
    size_t _mask;
    bool _overwrite;  // 덮어쓰기 모드
};

#endif