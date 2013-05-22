#ifndef _UTIL_SINGLE_PIN_DEBUG_MONITOR_2013_05_16_H_
  #define _UTIL_SINGLE_PIN_DEBUG_MONITOR_2013_05_16_H_

  #include <util/monitor/util_single_pin_debug_monitor_base.h>
  #include <util/utility/util_time.h>

  namespace util
  {
    template<typename port_pin_type>
    class single_pin_debug_monitor : public single_pin_debug_monitor_base
    {
    public:
      single_pin_debug_monitor();
      virtual ~single_pin_debug_monitor() { }
      void driver_task();

    private:
      static const std::uint_least16_t driver_delay_recv_init_microseconds = std::uint_least16_t(1000U - 50U);
      static const std::uint_least16_t driver_delay_recv_stat_microseconds = std::uint_least16_t(std::uint_least16_t(driver_delay_milliseconds) * 1000U) - 50U;

      typedef util::timer<std::uint16_t> timer_type;

      timer_type          driver_wait;
      std::uint_least8_t  driver_current_bit_position;

      void driver_init_port() const;
      void port_pin_send(const bool value_is_high) const;
      bool port_pin_receive() const;
      void driver_port_send();
      void driver_port_receive();
    };

    template<typename port_pin_type>
    single_pin_debug_monitor<port_pin_type>::single_pin_debug_monitor() : driver_wait(),
                                                                          driver_current_bit_position(0U)
    {
      port_pin_type::set_direction_input();
    }

    template<typename port_pin_type>
    void single_pin_debug_monitor<port_pin_type>::driver_task()
    {
      if(driver_is_in_send_mode)
      {
        driver_port_send();
      }
      else
      {
        // clear buffer automatically if timeout detected
        if(port_pin_receive())
        {
          ++driver_received_nothing_counter;

          if(driver_received_nothing_counter > driver_receive_reset)
          {
            driver_flush_buffer();
            driver_received_nothing_counter = 0U;
          }
        }
        else
        {
          driver_received_nothing_counter = 0U;
        }

        driver_port_receive();
      }
    }

    template<typename port_pin_type>
    void single_pin_debug_monitor<port_pin_type>::driver_init_port() const
    {
      (driver_is_in_send_mode ? port_pin_type::set_direction_output()
                              : port_pin_type::set_direction_input());
    }

    template<typename port_pin_type>
    void single_pin_debug_monitor<port_pin_type>::port_pin_send(const bool value_is_high) const
    {
      (value_is_high ? port_pin_type::set_pin_high() : port_pin_type::set_pin_low());
    }

    template<typename port_pin_type>
    bool single_pin_debug_monitor<port_pin_type>::port_pin_receive() const
    {
      return port_pin_type::read_input_value();
    }

    template<typename port_pin_type>
    void single_pin_debug_monitor<port_pin_type>::driver_port_send()
    {
      // Send the buffer contents over the port pin.
      // Use the UART physical layer protocol.
      if(driver_current_byte_value_or_position < driver_byte_buffer_length)
      {
        switch(driver_transmit_state)
        {
          case send_start_bit:
            if(driver_current_byte_value_or_position == 0U)
            {
              driver_init_port();
            }

            port_pin_send(false);
            driver_wait.start_relative(timer_type::microseconds(driver_delay_recv_stat_microseconds));
            driver_transmit_state = send_data_bits;
            driver_current_bit_position = 0U;

          case send_data_bits:
            if(driver_wait.timeout())
            {
              driver_wait.start_relative(timer_type::microseconds(driver_delay_recv_stat_microseconds));

              // Sent a single data bit.
              const bool bit_is_high = static_cast<std::uint_fast8_t>(driver_byte_buffer[driver_current_byte_value_or_position] & std::uint8_t(1U << driver_current_bit_position)) != static_cast<std::uint_fast8_t>(0U);

              port_pin_send(bit_is_high);

              ++driver_current_bit_position;

              if(driver_current_bit_position >= driver_byte_buffer.size())
              {
                driver_transmit_state = send_stop_bit;
              }
            }
            break;

          case send_stop_bit:
            if(driver_wait.timeout())
            {
              port_pin_send(true);
              driver_transmit_state = send_pause_for_next_byte;
              driver_wait.start_relative(timer_type::microseconds(driver_delay_recv_stat_microseconds));
            }
            break;

          case send_pause_for_next_byte:
            if(driver_wait.timeout())
            {
              ++driver_current_byte_value_or_position;

              driver_transmit_state = send_start_bit;

              if(driver_current_byte_value_or_position == driver_byte_buffer_length)
              {
                driver_is_in_send_mode = false;
                driver_flush_buffer();
                driver_init_port();
              }
            }
            break;

          default:
            break;
        }
      }
    }

    template<typename port_pin_type>
    void single_pin_debug_monitor<port_pin_type>::driver_port_receive()
    {
      // Read incoming data bytes on the port pin and store them in the buffer.
      // Use the UART physical layer protocol.
      if(driver_byte_buffer_length < driver_byte_buffer.size())
      {
        switch(driver_transmit_state)
        {
          case recieve_start_bit:
            if((!port_pin_receive()))
            {
              driver_wait.start_relative(timer_type::microseconds(driver_delay_recv_init_microseconds));
              driver_transmit_state = recieve_start_bit_validation;
            }
            break;

          case recieve_start_bit_validation:
            if(driver_wait.timeout())
            {
              if((!port_pin_receive()))
              {
                driver_wait.start_relative(timer_type::microseconds(driver_delay_recv_stat_microseconds));
                driver_current_bit_position           = 0U;
                driver_current_byte_value_or_position = 0U;
                driver_transmit_state                 = recieve_data_bits;
              }
              else
              {
                // If the start bit can not be validated, then
                // flush the buffer and revert to the receive state.
                driver_flush_buffer();
              }
            }
            break;

          case recieve_data_bits:
            if(driver_wait.timeout())
            {
              driver_wait.start_relative(timer_type::microseconds(driver_delay_recv_stat_microseconds));

              // Append the bit value to the current byte value.
              if(port_pin_receive())
              {
                driver_current_byte_value_or_position |= static_cast<std::uint8_t>(1U << driver_current_bit_position);
              }

              ++driver_current_bit_position;

              if(driver_current_bit_position >= 8U)
              {
                driver_transmit_state = recieve_stop_bit;
              }
            }
            break;

          case recieve_stop_bit:
            if(driver_wait.timeout())
            {
              if(port_pin_receive())
              {
                driver_byte_buffer[driver_byte_buffer_length] = driver_current_byte_value_or_position;

                ++driver_byte_buffer_length;
              }

              driver_transmit_state = recieve_pause_for_next_byte;
            }
            break;

          case recieve_pause_for_next_byte:
            driver_transmit_state = recieve_start_bit;
            break;

          default:
            break;
        }
      }
    }
  }

#endif // _UTIL_SINGLE_PIN_DEBUG_MONITOR_2013_05_16_H_
