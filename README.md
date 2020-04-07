Simple Maschine MIDI Driver
===========================

This application is simple userland driver for the Native Instruments
Maschine MIDI ports.

Since macOS Catalina, the Maschine MK I is not supported by the vendor
anymore, you can use this driver to use the MIDI ports on the back of the
machine.

Usage
-----

Use Xcode to compile the application. After it's run, you should be
able to see a new MIDI port in other MIDI applications.

Known Issues
------------

This application does not support USB hotplugging, and it is not tested
with more than one Maschine connected to the computer.

