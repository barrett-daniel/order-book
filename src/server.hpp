#pragma once

#include <boost/asio.hpp>

using boost::asio::ip::tcp;

// the below is from the boost asio tutorial: https://www.boost.org/doc/user-guide/task-networking.html#_sample_client_server_messaging

class Server {
public:
    Server() {
        try {
            // io_context is the central object used by Boost.Asio.
            //
            // Many Asio programs use io_context to run asynchronous
            // operations. This particular example uses synchronous
            // operations, so io_context doesn't need to run an event loop.
            boost::asio::io_context io_context;

            // Create a TCP acceptor.
            //
            // The acceptor listens for incoming TCP connections.
            //
            // tcp::v4() means IPv4.
            //
            // Port 12345 is the TCP port on which this server will listen.
            tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 12345));

            // Tell the user that the server is ready for a connection.
            std::cout << "Server started. Waiting for client..." << std::endl;

            // Create a socket that will eventually represent the connection to the client.
            tcp::socket socket(io_context);

            // Wait for a client to connect.
            //
            // accept() is synchronous, so the program stops here until
            // a client establishes a TCP connection.
            //
            // When a connection arrives, Asio places the connected socket into the 'socket' object.
            acceptor.accept(socket);

            std::cout << "Client connected!" << std::endl;

            // Transfer ownership of the socket to handle_client().
            //
            // std::move() allows the socket to be moved rather than copied.
            // This is necessary because a socket represents a network
            // resource that should have one clear owner.
            handle_client(std::move(socket));

        } catch (std::exception& e) {
            // Catch errors that occur while setting up the server or accepting the connection.
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }

    void handle_client(tcp::socket socket) {
        try {

            // A simple buffer for data received from the client.
            // The server can receive up to 1023 characters at a time
            // because one byte is reserved for the terminating '\0'.
            char data[1024];

            // Keep communicating with the client until the connection is closed or an error occurs.
            while (true) {

                // Clear the buffer before receiving the next message.
                // memset() fills the specified memory with zero bytes.
                std::memset(data, 0, sizeof(data));

                // Boost.Asio normally reports errors through an
                // error_code object rather than throwing an exception.
                boost::system::error_code error;

                // Read some data from the TCP socket.
                //
                // boost::asio::buffer(data) tells Asio that 'data' is
                // the memory into which the received bytes should be placed.
                //
                // read_some() is a synchronous operation: this thread
                // waits here until some data arrives or an error occurs.
                size_t length = socket.read_some(boost::asio::buffer(data), error);

                // EOF means the other end has closed the connection.
                if (error == boost::asio::error::eof) break; // Connection closed

                // Any other error is treated as an exception.
                // system_error converts the error_code into a C++ exception
                // containing useful diagnostic information.
                else if (error) throw boost::system::system_error(error);

                // Display the message received from the client.
                std::cout << "Client: " << data << std::endl;

                // Prepare a response to send back to the client.
                std::string response;

                // Ask the person running the server what they want to send.
                std::cout << "You: ";
                std::getline(std::cin, response);

                // Send the response to the client.
                //
                // boost::asio::buffer(response) creates a view of the string's memory for Asio to send.
                //
                // write() is synchronous, so this thread waits until the response has been written to the socket.
                boost::asio::write(socket, boost::asio::buffer(response), error);
            }

        // Catch any exception thrown while communicating with the client.
        } catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }
};