** Event Notification

This module handles sending out real-time notifications, via MQTT, to any listeners.
The topic name comprises of the ThingName appended with ** /Events
This event notification mechanism is intended to be used to convey information like:

 - Dispense progress
 - System status