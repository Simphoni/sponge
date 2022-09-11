#include "tcp_connection.hh"

#include <algorithm>
#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _receiver.stream_out().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (_status == 3)
        return;
    if (seg.header().rst) {  // reset stream
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _status = 3;
        return;
    }
    if (!_status && seg.header().syn) {
        _status = 1;
    }
    // by SYN received, the following packets must have ackno
    fprintf(stderr, "%p %d\n", static_cast<void*>(this), _status);
    if (!_status) {
        return;  // stream must be open
    }
    if (seg.header().fin) {
        _close_status |= 1;
        if (!(_close_status & 2)) {  // passive close
            _linger_after_streams_finish = false;
        }
    }
    // give the receiver the buffer && seqno
    _receiver.segment_received(seg);
    // give the sender ackno && window size
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        if (_sendseq_tail && _sendseq_tail.value() == seg.header().ackno)
            _close_status |= 4;
    }
    _sender.fill_window();
    auto &q = _sender.segments_out();
    if (q.empty()) {                           // nothing to send
        if (seg.length_in_sequence_space()) {  // incoming segment occupied any sequence numbers
            _sender.send_empty_segment();
        } else if (_receiver.ackno() && seg.header().seqno == _receiver.ackno().value() - 1) {  // keep-alive request
            _sender.send_empty_segment();
        }
    }
    fprintf(stderr,
            "!!!%p %u %u+%u,%d\n",
            (void*)this,
            _receiver.ackno().value().raw_value(),
            seg.header().seqno.raw_value(),
            seg.payload().str().size(),q.size());
    while (q.size()) {
        auto &packet = q.front();
        packet.header().ack = 1;
        packet.header().ackno = _receiver.ackno().value();
        packet.header().win = min(_receiver.window_size(), static_cast<size_t>(numeric_limits<uint16_t>::max()));
        _segments_out.push(packet);
        q.pop();
    }
    _time_since_last_segment_received = 0;
    if (_close_status == 7)
        _status = 3 - _linger_after_streams_finish;  // will be 3 if linger is false
}

bool TCPConnection::active() const { return _status == 0 || _status == 1 || _status == 2; }

size_t TCPConnection::write(const string &data) {
    size_t tot = 0, prev_written = 1;
    while (tot < data.length()) {
        // writes into stream
        size_t bytes_written = _sender.stream_in().write(data.substr(tot));
        tot += bytes_written;
        if (bytes_written == 0 && prev_written == 0)
            break;
        prev_written = bytes_written;
        // generate packages
        _sender.fill_window();
        auto &q = _sender.segments_out();
        while (q.size()) {
            auto &packet = q.front();
            packet.header().ack = 1;
            packet.header().ackno = _receiver.ackno().value();
            packet.header().win = min(_receiver.window_size(), static_cast<size_t>(numeric_limits<uint16_t>::max()));
            _segments_out.push(packet);
            q.pop();
        }
    }
    fprintf(stderr, "%u %u %u\n", tot, data.length(), _sender.stream_in().remaining_capacity());
    return tot;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (_status != 1 && _status != 2)
        return;
    _time_since_last_segment_received += ms_since_last_tick;
    if (_status == 2 && _time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
        _status = 3;
        return;
    }
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        cerr << "Warning: Unclean shutdown due to too many retrans\n";
        _sender.send_empty_segment();
        auto &packet = _sender.segments_out().front();
        packet.header().rst = true;
        packet.header().ack = true;
        _segments_out.push(packet);
        _sender.segments_out().pop();
        _status = 3;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
    } else {
        _sender.fill_window();  // after _sender.tick(), new segments are appended
        auto &q = _sender.segments_out();
        while (q.size()) {
            auto &packet = q.front();
            packet.header().ack = 1;
            packet.header().ackno = _receiver.ackno().value();
            packet.header().win = min(_receiver.window_size(), static_cast<size_t>(numeric_limits<uint16_t>::max()));
            _segments_out.push(packet);
            q.pop();
        }
    }
}

void TCPConnection::end_input_stream() {
    if (_status >= 2)
        return;
    _sender.stream_in().end_input();
    _close_status |= 2;
    _sender.fill_window();
    _sendseq_tail = std::make_optional(_sender.next_seqno());
    auto &q = _sender.segments_out();
    while (q.size()) {
        auto &packet = q.front();
        packet.header().ack = 1;
        packet.header().ackno = _receiver.ackno().value();
        packet.header().win = min(_receiver.window_size(), static_cast<size_t>(numeric_limits<uint16_t>::max()));
        _segments_out.push(packet);
        q.pop();
    }
    if (_close_status == 7) {
        _status = 3 - _linger_after_streams_finish;
        _time_since_last_segment_received = 0;
    }
}

void TCPConnection::connect() {
    _sender.fill_window();  // will generate a SYN if _next_seqno == 0
    auto &q = _sender.segments_out();
    q.front().header().win = min(_receiver.window_size(), static_cast<size_t>(numeric_limits<uint16_t>::max()));
    _segments_out.push(q.front());
    q.pop();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection due to destruction\n";
            _sender.send_empty_segment();
            auto &packet = _sender.segments_out().front();
            packet.header().rst = true;
            _segments_out.push(packet);
            _sender.segments_out().pop();
            _status = 3;
            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
