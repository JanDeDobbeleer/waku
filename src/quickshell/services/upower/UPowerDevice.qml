import QtQuick

// Quickshell.Services.UPower's UPowerDevice. UPower and PowerProfiles create the instances; the
// values come from the battery service, and what Windows does not report stays 0.
QtObject {
    final property real changeRate: 0
    final property int type: UPowerDeviceType.Unknown
    final property real healthPercentage: 0
    final property real energyCapacity: 0
    final property real energy: 0
    final property bool isLaptopBattery: type === UPowerDeviceType.Battery && powerSupply
    final property bool powerSupply: false
    final property bool ready: false
    final property bool healthSupported: false
    final property string model: ""
    final property string nativePath: ""
    final property int state: UPowerDeviceState.Unknown
    final property bool isPresent: false
    // The charge as a fraction, 0 to 1 (UPower's energy / energyCapacity), not a percentage.
    final property real percentage: 0
    final property real timeToFull: 0
    final property real timeToEmpty: 0
    final property string iconName: ""
}
