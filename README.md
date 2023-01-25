Senders and Receiver for the GNOME Glib library 
===============================================

This is a small C++ library that glues together the Glib event loop and the current executor proposal for C++.
It can be used to compose asynchronous tasks in a Glib event loop.

Since single-valued senders can be automatically converted to awaitable objects, the library also provides a way to use C++ coroutines in a Glib event loop.

The library requires C++20.

Example

```cpp