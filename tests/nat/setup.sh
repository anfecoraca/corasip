#!/bin/sh
# M5C test path: a network namespace "natc" behind masquerade on this host.
#   host side  vnat-h  10.99.0.1/24 (this machine)
#   inside     vnat-c  10.99.0.2/24, default route via 10.99.0.1, in namespace natc
# Traffic from natc leaves this host with the host's address (NAT). Root only. Idempotent.
set -eu
WAN=$(ip route get 192.0.2.10 | awk '{for(i=1;i<=NF;i++) if($i=="dev") print $(i+1)}')
ip netns list | grep -qx natc || ip netns add natc
ip link show vnat-h >/dev/null 2>&1 || ip link add vnat-h type veth peer name vnat-c
ip link set vnat-c netns natc 2>/dev/null || true
ip addr replace 10.99.0.1/24 dev vnat-h
ip link set vnat-h up
ip netns exec natc ip addr replace 10.99.0.2/24 dev vnat-c
ip netns exec natc ip link set vnat-c up
ip netns exec natc ip link set lo up
ip netns exec natc ip route replace default via 10.99.0.1
sysctl -qw net.ipv4.ip_forward=1
iptables -t nat -C POSTROUTING -s 10.99.0.0/24 -o "$WAN" -j MASQUERADE 2>/dev/null || iptables -t nat -A POSTROUTING -s 10.99.0.0/24 -o "$WAN" -j MASQUERADE
iptables -C FORWARD -i vnat-h -o "$WAN" -j ACCEPT 2>/dev/null || iptables -A FORWARD -i vnat-h -o "$WAN" -j ACCEPT
iptables -C FORWARD -i "$WAN" -o vnat-h -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || iptables -A FORWARD -i "$WAN" -o vnat-h -m state --state RELATED,ESTABLISHED -j ACCEPT
echo "natc ready: wan=$WAN"
