Coming from the world of low latency C++ systematic trading and devling into crypto trading, I found a derth of modern options for http and websocket developement. 
Many of the existing options like Boost Beast were older and did not leverage the modern language adavancements that have manifested in C++. For this reason I 
created Medici (Modern Event Driven Interfaces for c++ I/O). Medici is a C++ project in the same spirit as Boost ASIO. It provides thread management, event queues, 
timers, and a comprehensive set of socket implementations for both client and server endpooints. At the moment the project is still in the pre release stage, however
there is significant functional core that is currently driving applications. Some of these apps will be showcased in this readme leading up to the initial realease. 
