pragma Singleton
import QtQuick

// Quickshell.Services.UPower's UPowerDeviceType (the values are UPower's).
QtObject {
    enum Type {
        Unknown = 0,
        LinePower = 1,
        Battery = 2,
        Ups = 3,
        Monitor = 4,
        Mouse = 5,
        Keyboard = 6,
        Pda = 7,
        Phone = 8,
        MediaPlayer = 9,
        Tablet = 10,
        Computer = 11,
        GamingInput = 12,
        Pen = 13,
        Touchpad = 14,
        Modem = 15,
        Network = 16,
        Headset = 17,
        Speakers = 18,
        Headphones = 19,
        Video = 20,
        OtherAudio = 21,
        RemoteControl = 22,
        Printer = 23,
        Scanner = 24,
        Camera = 25,
        Wearable = 26,
        Toy = 27,
        BluetoothGeneric = 28
    }

    function toString(type) {
        const names = ["Unknown", "LinePower", "Battery", "Ups", "Monitor", "Mouse", "Keyboard", "Pda", "Phone", "MediaPlayer", "Tablet", "Computer", "GamingInput", "Pen", "Touchpad", "Modem", "Network", "Headset", "Speakers", "Headphones", "Video", "OtherAudio", "RemoteControl", "Printer", "Scanner", "Camera", "Wearable", "Toy", "BluetoothGeneric"];
        return names[type] !== undefined ? names[type] : "Invalid";
    }
}
