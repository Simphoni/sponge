#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _cur_retrans_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _acked; }

void TCPSender::fill_window() {
    if (_fin_sent)
        return;
    if (_next_seqno == 0) {
        // not synced -> send SYN
        TCPSegment packet;
        packet.header().syn = 1;
        packet.header().seqno = _isn;
        _next_seqno = 1;
        _flying_packets.push(make_pair(0, packet));
        _segments_out.push(packet);
        clock.restart(_initial_retransmission_timeout);
        return;
    }
    auto p = _window_size;
    if (_window_size == 0)
        _window_size = 1;  // treat size `0` as `1`
    while (_next_seqno < _acked + _window_size && (_stream.buffer_size() || _stream.input_ended())) {
        TCPSegment packet;
        size_t len = min(_stream.buffer_size(), _acked + _window_size - _next_seqno);
        len = min(len, TCPConfig::MAX_PAYLOAD_SIZE);
        if (_stream.input_ended() && len == _stream.buffer_size() && len < _acked + _window_size - _next_seqno)
            packet.header().fin = true;  // sent_bytes = len + 1

        string s = _stream.read(len);
        packet.payload() = Buffer(move(s));
        packet.header().seqno = this->next_seqno();
        _flying_packets.push(make_pair(_next_seqno, packet));
        _segments_out.push(packet);

        _next_seqno += len + packet.header().fin;

        if (packet.header().fin) {
            _fin_sent = true;
            break;
        }
    }
    _window_size = p;
    if (!_flying_packets.empty() && !clock.running())
        clock.restart(_initial_retransmission_timeout);
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t tmp = unwrap(ackno, _isn, _acked);
    if (tmp > _next_seqno)
        return;  // Impossible ackno (beyond next seqno) is ignored
    // reset RTO
    _consecutive_timeout = 0;
    _cur_retrans_timeout = _initial_retransmission_timeout;

    if (tmp > _acked) {  // new data acknowledged
        _acked = tmp;
        clock.restart(_initial_retransmission_timeout);
    }
    if (!_fin_sent)
        _window_size = window_size;
    while (!_flying_packets.empty() &&
           _flying_packets.front().first + _flying_packets.front().second.length_in_sequence_space() <= _acked)
        _flying_packets.pop();  // make sure whole segment is acked
    if (_flying_packets.empty())
        clock.stop();
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (clock.ellapse(ms_since_last_tick)) {  // resend
        if (_window_size > 0) {
            _consecutive_timeout++;
            _cur_retrans_timeout *= 2;
        }
        clock.restart(_cur_retrans_timeout);
        _segments_out.push(_flying_packets.front().second);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_timeout; }

void TCPSender::send_empty_segment() {
    TCPSegment packet;
    packet.payload() = Buffer("");
    packet.header().seqno = this->next_seqno();
    _segments_out.push(packet);
}
