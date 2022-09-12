#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

#include "iostream"

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (!_isn.has_value() && seg.header().syn)
        _isn = std::make_optional<WrappingInt32>(seg.header().seqno);
    if (!_isn.has_value())
        return;  // SYN not received
    _reassembler.push_substring(
        seg.payload().copy(),
        unwrap(seg.header().seqno + seg.header().syn - 1, _isn.value(), _reassembler.stream_out().bytes_written()),
        seg.header().fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_isn)
        return std::nullopt;
    return make_optional<WrappingInt32>(*_isn + _reassembler.stream_out().bytes_written() +
                                        _reassembler.stream_out().input_ended() + 1);  // FIN + SYN
}

size_t TCPReceiver::window_size() const { return _reassembler.stream_out().remaining_capacity(); }
