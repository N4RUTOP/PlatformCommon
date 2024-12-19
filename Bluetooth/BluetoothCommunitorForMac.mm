#include "IBluetoothCommunitor.h"

using BluetoothError = IBluetoothCommunitor::BluetoothError;

void BluetoothCommunitorForMac::setBluetoothAddress(const std::string& addr)
{
    
}

BluetoothError BluetoothCommunitorForMac::isPair(bool& pair)
{
    return BTH_E_SUCCESS;
}

BluetoothError BluetoothCommunitorForMac::pair()
{
    return BTH_E_SUCCESS;
}

BluetoothError BluetoothCommunitorForMac::connect()
{
    return BTH_E_SUCCESS;
}

BluetoothError BluetoothCommunitorForMac::send(const std::vector<uint8_t>& data)
{
    return BTH_E_SUCCESS;
}

BluetoothError BluetoothCommunitorForMac::recv(std::vector<uint8_t>& data)
{
    return BTH_E_SUCCESS;
}

BluetoothError BluetoothCommunitorForMac::disconnect()
{
    return BTH_E_SUCCESS;
}
