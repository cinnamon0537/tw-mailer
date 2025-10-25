#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <sys/socket.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <arpa/inet.h>

/**
 * @brief Sends all data in the buffer, handling partial sends
 * @param fd Socket file descriptor
 * @param buf Pointer to data to send
 * @param len Number of bytes to send
 * @return true if all data sent successfully, false on error or closed connection
 */
bool send_all(int fd, const void* buf, size_t len);

/**
 * @brief Receives exactly len bytes into the buffer
 * @param fd Socket file descriptor  
 * @param buf Pointer to buffer for received data
 * @param len Number of bytes to receive
 * @return true if all data received successfully, false on error or closed connection
 */
bool recv_exact(int fd, void* buf, size_t len);

/**
 * @brief Receives a length-prefixed block of data
 * @param fd Socket file descriptor
 * @param out String to store received data
 * @return true if block received successfully, false on error
 */
bool recv_block(int fd, std::string& out);

/**
 * @brief Sends a length-prefixed block of data
 * @param fd Socket file descriptor
 * @param msg String containing data to send
 * @return true if block sent successfully, false on error
 */
bool send_block(int fd, const std::string& msg);

#endif // NETWORK_UTILS_H