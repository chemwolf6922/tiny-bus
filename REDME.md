# WIP
This is not fully tested yet.
# Tiny bus
This is a pub/sub bus for IPC purpose on Linux. 
## Features
* Supports abstract unix domain socket.
* Reduced memory footprint for each transferred message.
* Fast client side callback. Can be faster though.
* Client is NOT thread safe. This is meant to be used in a event loop application.
## Dependencies
This project uses [tev](https://github.com/chemwolf6922/tiny-event-loop) as its event loop.
