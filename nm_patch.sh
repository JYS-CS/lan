sed -i 's/m_gatewayMac = mac;/setGatewayMac(mac);/g' core/NetworkManager.cpp
sed -i 's/m_gatewayMac = parts\[3\];/setGatewayMac(parts[3]);/g' core/NetworkManager.cpp
