#!/bin/sh
# M5C: a STUN server in namespace stunsrv (172.31.0.2) on a veth to this host (172.31.0.1). Traffic from natc
# to the STUN server is masqueraded out of the host's veth, so the STUN server sees 172.31.0.1 as the public
# address: a real reflexive-candidate test. Root only. Idempotent.
set -eu
ip netns list | grep -qx stunsrv || ip netns add stunsrv
ip link show vnat-sh >/dev/null 2>&1 || ip link add vnat-sh type veth peer name vnat-ss
ip link set vnat-ss netns stunsrv 2>/dev/null || true
ip addr replace 172.31.0.1/24 dev vnat-sh
ip link set vnat-sh up
ip netns exec stunsrv ip addr replace 172.31.0.2/24 dev vnat-ss
ip netns exec stunsrv ip link set vnat-ss up
ip netns exec stunsrv ip link set lo up
iptables -t nat -C POSTROUTING -s 10.99.0.0/24 -d 172.31.0.2 -j MASQUERADE 2>/dev/null || iptables -t nat -A POSTROUTING -s 10.99.0.0/24 -d 172.31.0.2 -j MASQUERADE
iptables -C FORWARD -i vnat-h -o vnat-sh -j ACCEPT 2>/dev/null || iptables -A FORWARD -i vnat-h -o vnat-sh -j ACCEPT
iptables -C FORWARD -i vnat-sh -o vnat-h -j ACCEPT 2>/dev/null || iptables -A FORWARD -i vnat-sh -o vnat-h -j ACCEPT
ip netns exec natc ip route replace 172.31.0.0/24 via 10.99.0.1
echo "stunsrv ready at 172.31.0.2"
