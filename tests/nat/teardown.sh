#!/bin/sh
# Removes the M5C test path. Root only. Safe to run twice.
set -u
WAN=$(ip route get 192.0.2.10 | awk '{for(i=1;i<=NF;i++) if($i=="dev") print $(i+1)}')
iptables -t nat -D POSTROUTING -s 10.99.0.0/24 -o "$WAN" -j MASQUERADE 2>/dev/null
iptables -D FORWARD -i vnat-h -o "$WAN" -j ACCEPT 2>/dev/null
iptables -D FORWARD -i "$WAN" -o vnat-h -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null
ip netns del natc 2>/dev/null
ip link del vnat-h 2>/dev/null
echo "natc removed"
