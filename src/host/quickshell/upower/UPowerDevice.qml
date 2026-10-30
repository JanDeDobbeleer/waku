import QtQuick

// Quickshell.Services.UPower's UPowerDevice. UPower and PowerProfiles create the instances; the
// values come from the battery service, and what Windows does not report stays 0.
QtObject {
    property real changeRate: 0
    property int type: UPowerDeviceType.Unknown
    property real healthPercentage: 0
    property real energyCapacity: 0
    property real energy: 0
    property bool isLaptopBattery: type === UPowerDeviceType.Battery && powerSupply
    property bool powerSupply: false
    property bool ready: false
    property bool healthSupported: false
    property string model: ""
    property string nativePath: ""
    property int state: UPowerDeviceState.Unknown
    property bool isPresent: false
    // The charge as a fraction, 0 to 1 (UPower's energy / energyCapacity), not a percentage.
    property real percentage: 0
    property real timeToFull: 0
    property real timeToEmpty: 0
    property string iconName: ""
}
