#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    EthernetFrame frame;
    EthernetHeader &hdr = frame.header();
    hdr.type = EthernetHeader::TYPE_IPv4;
    hdr.src = _ethernet_address;
    frame.payload() = dgram.serialize();
    // check cache
    auto it = _arp_cache.find(next_hop_ip);
    if (it != _arp_cache.end() && it->second.second > _expire_clk) {
        hdr.dst = it->second.first;
        _frames_out.push(frame);
    } else {
        _pending.emplace_back(next_hop_ip, make_shared<EthernetFrame>(move(frame)));
        if (_arp_inq.find(next_hop_ip) != _arp_inq.end())
            return;
        _arp_inq.insert(next_hop_ip);
        ARPMessage msg;
        msg.opcode = ARPMessage::OPCODE_REQUEST;
        msg.sender_ip_address = _ip_address.ipv4_numeric();
        msg.sender_ethernet_address = _ethernet_address;
        msg.target_ip_address = next_hop_ip;
        EthernetFrame arp_frm;
        EthernetHeader &arp_hdr = arp_frm.header();
        arp_hdr.type = EthernetHeader::TYPE_ARP;
        arp_hdr.src = _ethernet_address;
        arp_hdr.dst = ETHERNET_BROADCAST;
        arp_frm.payload() = msg.serialize();
        _frames_out.push(arp_frm);
        _arp_msgs.push(make_pair(make_pair(_expire_clk + 5000, next_hop_ip), arp_frm));
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    const EthernetHeader &hdr = frame.header();
    if (hdr.dst != _ethernet_address && hdr.dst != ETHERNET_BROADCAST)
        return nullopt;
    // refresh cache && try to send
    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage msg;
        ParseResult pres = msg.parse(frame.payload());
        if (pres != ParseResult::NoError)
            return nullopt;
        uint32_t map_ip = msg.sender_ip_address;
        EthernetAddress map_ether = msg.sender_ethernet_address;
        _arp_cache[map_ip] = make_pair(map_ether, _expire_clk + 30000);
        if (msg.opcode == ARPMessage::OPCODE_REQUEST && msg.target_ip_address == _ip_address.ipv4_numeric()) {
            EthernetFrame arp_frm;
            EthernetHeader &arp_hdr = arp_frm.header();
            arp_hdr.type = EthernetHeader::TYPE_ARP;
            arp_hdr.src = _ethernet_address;
            arp_hdr.dst = map_ether;
            msg.opcode = ARPMessage::OPCODE_REPLY;
            msg.target_ethernet_address = map_ether;
            msg.target_ip_address = map_ip;
            msg.sender_ethernet_address = _ethernet_address;
            msg.sender_ip_address = _ip_address.ipv4_numeric();
            arp_frm.payload() = msg.serialize();
            _frames_out.push(arp_frm);
        }
        for (size_t i = 0; i < _pending.size();) {
            if (_pending[i].first != map_ip) {
                i++;
                continue;
            }
            _pending[i].second->header().dst = map_ether;
            _frames_out.push(move(*_pending[i].second));
            _pending[i] = _pending.back();
            _pending.pop_back();
        }
        return nullopt;
    } else {
        InternetDatagram dgram;
        ParseResult pres = dgram.parse(frame.payload());
        if (pres == ParseResult::NoError)
            return make_optional(dgram);
        else
            return nullopt;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _expire_clk += ms_since_last_tick;
    while (!_arp_msgs.empty()) {
        auto x = _arp_msgs.front();
        if (x.first.first > _expire_clk)
            break;
        uint32_t target_ip = x.first.second;

        auto it = _arp_cache.find(target_ip);
        if (it != _arp_cache.end() && it->second.second > _expire_clk) {
            // request already responded
            _arp_inq.erase(target_ip);
        } else {
            _frames_out.push(x.second);
            x.first.first = _expire_clk + 5000;
            _arp_msgs.push(x);
        }
        _arp_msgs.pop();
    }
}
