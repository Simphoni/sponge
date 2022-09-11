#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : cap(capacity) {
    // make sure rear < front on the next round
    buffer.assign(capacity + 1, '\0');
}

size_t ByteStream::write(const string &data) {
    if (_eof || _error)
        return 0;  // reject writing attempt
    int len = std::min(remaining_capacity(), data.length());
    nwrite += len;
    if (front <= rear) {
        int p = cap - rear + 1;
        if (p <= len) {  // rear wraps round
            buffer.replace(rear, p, data, 0, p);
            buffer.replace(0, len - p, data, p, len - p);
            rear = len - p;
        } else {
            buffer.replace(rear, len, data, 0, len);
            rear += len;
        }
    } else {
        buffer.replace(rear, len, data, 0, len);
        rear += len;
    }
    return len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    if (_error)
        return "";
    int l = std::min(len, buffer_size());
    string s;
    if (front <= rear) {
        s = buffer.substr(front, l);
    } else {
        int p = cap + 1 - front;
        if (p <= l) {
            s = buffer.substr(front, p) + buffer.substr(0, l - p);
        } else {
            s = buffer.substr(front, l);
        }
    }
    return s;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    int l = std::min(len, buffer_size());
    nread += l;
    front = (front + l) % (cap + 1);
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    if (_error)
        return "";
    int l = std::min(len, buffer_size());
    nread += l;
    string s;
    if (front <= rear) {
        s = buffer.substr(front, l);
        front += l;
    } else {
        int p = cap + 1 - front;
        if (p <= l) {
            s = buffer.substr(front, p) + buffer.substr(0, l - p);
            front = l - p;
        } else {
            s = buffer.substr(front, l);
            front += l;
        }
    }
    return s;
}

void ByteStream::end_input() { _eof = true; }

bool ByteStream::input_ended() const { return _eof; }

size_t ByteStream::buffer_size() const {
    if (front <= rear)
        return rear - front;
    else
        return cap + 1 - (front - rear);
}

bool ByteStream::buffer_empty() const { return front == rear; }

bool ByteStream::eof() const { return _eof && (front == rear); }

size_t ByteStream::bytes_written() const { return nwrite; }

size_t ByteStream::bytes_read() const { return nread; }

size_t ByteStream::remaining_capacity() const {
    if (front <= rear)
        return cap - (rear - front);
    else
        return front - rear - 1;
}
