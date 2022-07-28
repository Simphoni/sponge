#include "stream_reassembler.hh"

#include "iostream"
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

#define WRAP(n) ((n) >= _capacity ? (n) - _capacity : (n))

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {
    _buffer.assign(_capacity, '\0');
    _used.init(_capacity);
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    size_t start = 0, tmp = 0;
    if (index < _pending_idx)
        start = _pending_idx - index;
    if (start < data.length() && index < _pending_idx + _capacity) {
        // will surely insert sth
        size_t term = std::min(index + data.length(), _pending_idx + _capacity) - index;
        size_t offset = WRAP(_headptr + index + start - _pending_idx);
        while (start < term) {
            if (!_used.get(offset)) {
                _unass_bytes++;
                _used.set(offset);
                _buffer[offset] = data[start];
            }
            start++;
            offset++;
            offset = WRAP(offset);
        }
    }
    // update _output
    for (int t = 0; t < 2; t++) {
        start = _headptr;
        while (start < _capacity && _used.get(start))
            start++;
        if (start != _headptr) {
            tmp = _output.write(_buffer.substr(_headptr, start - _headptr));
            if (tmp) {
                for (size_t r = 0; r < tmp; r++)
                    _used.unset(_headptr + r);
                _unass_bytes -= tmp;
                _pending_idx += tmp;
                _headptr = WRAP(_headptr + tmp);
            }
        }
    }
    if (eof && !_eof) {
        // accept only the first eof
        _eof = true;
        _endidx = index + data.length();
    }
    if (_eof && _pending_idx >= _endidx)
        _output.end_input();
}

size_t StreamReassembler::unassembled_bytes() const { return _unass_bytes; }

bool StreamReassembler::empty() const { return _unass_bytes == 0; }

#undef WRAP