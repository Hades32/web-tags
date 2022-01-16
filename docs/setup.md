# Configuration

TODO: Describe activation process in more detail

1. go to http://web-tag.local/
2. configure `FreqID`, `NetID`, `Slot` or just keep the default and then click "save settings"
  * if nothing happens and the end of this procedure you also need to configure `Frequency Offset`
3. Add your devices address (looks like a standard MAC address and can be found on the back of the display)
4. assing a random ID from the range 1-127 (remember it. This is the ID that you alter need to send messages)
5. Click `Wake + Full Sync`
6. Wait for status to show `Idle` for several seconds
7. Click activate
8. Wait for success

## Meaning of the configuration values:

* `FreqID`: there's a list of 72 pre-defined frequencies by the manufacturer. This ID selects one of them
* `NetID`: just like a WiFi SSID. Must be different from your neighbor, but many displays that you own can have the same one.
* `Slot`: To save power, displays only listen to messages `100% / num_of_slots` of the time. So the more slots you define, the slower a device will react, but the more power it can save. The slot number it wakes up on, is calculated from the display ID (maybe something like `my_slot = my_id % num_of_slots`).
* `Frequency Offset`: many chips/antennas don't accurately produce frequencies. This number moves the frequencies a bit, to accompensate for this issue. My China modules needed a value of `17` here.

If you change any of these you need to re-activate your device. (If the UI shows something different, that doesn't matter. It doesn't remember the actual settings. You can click "get settings" to retrieve the current values)
