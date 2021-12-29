#pragma once
#include "RFV3.h"

class ModeTrans : public mode_class
{
public:
  virtual void interrupt()
  {
    switch (tx_state)
    {
    case 1:
      cc1101_rx(0);
      tx_state = 3;
      rx_start_time = millis();
      break;
    case 3: //Rad the data from RX buffer
      read_data_cc1101();
      tx_state = 0;
      break;
    case 40: // more than one part so reffiling new buffer
      tx_state = 1;
      tx_data_main();
      break;
    case 50:
      save_receive_buffer();
      set_is_data_waiting(0);
      cc1101_idle();
      tx_state = 52;

      if (get_trans_mode())
      {
        set_trans_mode(0); // New activation mode finished
        set_last_activation_status(3);
        restore_current_settings(); // Go back to regular channel and network
        reset_full_sync_count();
        set_mode_full_sync(); // Full Sync after new activation
      }
      else
      {
        set_mode_idle();
        set_last_send_status(3);
      }
      break;
    }
  }

  virtual void new_interval()
  {
    log("to long, back to idle");
    set_mode_idle();
  }

  virtual void pre()
  {
    log(mode_name);
    if (get_last_to_short())
    {
      set_last_to_short(0);
      log("Last one was to short so continue");
    }
    else
    {
      if (get_trans_mode())
      {
        id_offset = 4;
        max_cur_packet_len = 56;
      }
      else
      {
        id_offset = 0;
        max_cur_packet_len = 57;
      }
      packet_counter = 0;
      packet_counter_rx = 0;
      last_packet = false;
      is_first = true;
      multi_tx = false;
      curr_data_position = 0;
      still_to_send = 0;
      number_of_con_data = 0;
      timeout_counter = 0;
      tx_state = 0;
    }
  }

  virtual void main()
  {
    switch (tx_state)
    {
    case 0: //Handle new beginning
      time_left = 1100 - (millis() - get_last_slot_time());
      log("TIME LEFT: " + String(time_left));
      if (time_left < 250)
      {
        set_last_to_short(true);
        cc1101_idle();
        set_mode_idle();
      }
      else
      {
        tx_data_main();
        if (get_rx_enable())
        {
          tx_state = 1;
        }
        else
        {
          if (last_packet)
            tx_state = 50;
        }
      }
      break;
    case 3: //Waiting for RX data here
      if (millis() - rx_start_time >= get_rx_timeout())
      {
        log("RX_TIMEOUT!!!");
        timeout_counter++;
        check_timeout();
        tx_state = 0;
        curr_data_position = last_position_to_go_back;
        packet_counter = last_position_to_go_back_counter;
      }
      break;
    }
  }

  virtual String get_name()
  {
    return mode_name;
  }

private:
  String mode_name = "Transmission";

  int time_left;

  int curr_data_position = 0;
  bool is_first = true;
  int still_to_send = 0;

  bool display_more_data = false;
  bool display_more_than_one = false;

  int number_of_con_data = 0;

  volatile int tx_state = 0;
  volatile long rx_start_time;

  bool multi_tx = false;
  bool last_packet = false;
  uint8_t packet_counter = 0;
  uint8_t packet_counter_rx = 0;

  uint8_t tx_data_buffer_int[62];
  bool next_rx_enable = false;

  int max_cur_packet_len = 57; // Real data is shorter if new Activation is used
  int id_offset = 0;           /*When using the new Activation the ID = Serial so 6 instead of 2 bytes*/

  int last_position_to_go_back = 0;
  int last_position_to_go_back_counter = 0;
  int ack_cont_data = 0;

  int timeout_counter = 0;

  void tx_data_main()
  {
    log("TX data main");
    int curr_packet_len = 0;

    memset(tx_data_buffer_int, 0x00, 62);
    tx_data_buffer_int[0] = get_network_id();

    if (id_offset > 0)
    {
      uint8_t serial[7];
      get_serial(serial);
      memcpy(&tx_data_buffer_int[1], serial, 6);
      /* Serial from 1 - 6 */
    }
    else
    {
      tx_data_buffer_int[1] = get_display_id() >> 8;
      tx_data_buffer_int[2] = get_display_id() & 0xff;
    }
    set_rx_enable(true);
    still_to_send = get_len_send() - curr_data_position;
    set_still_to_send(still_to_send);

    if (still_to_send > 0)
    {
      if (is_first)
      {

        tx_data_buffer_int[id_offset + 3] = packet_counter;
        Serial.println("Is first");
        is_first = false;
        number_of_con_data = still_to_send / (max_cur_packet_len - id_offset);
        if (number_of_con_data > 7)
          number_of_con_data = 7;
        Serial.println("Cont data: " + String(number_of_con_data));
        Serial.println("Still to send: " + String(still_to_send));

        //Fill variables to go back to last part when displays did not received all
        last_position_to_go_back = curr_data_position;
        last_position_to_go_back_counter = packet_counter;
        ack_cont_data = number_of_con_data;
      }
      else
      {

        tx_data_buffer_int[id_offset + 3] = packet_counter;
        Serial.println("Is continiuos");
        set_rx_enable(false);
        if (number_of_con_data == 0)
        {
          is_first = true;
          set_rx_enable(true);
          packet_counter = packet_counter + 1;
          packet_counter &= 0x0F;
        }
      }

      /*Copy the actual data into the TX buffer*/
      if (still_to_send > (max_cur_packet_len - id_offset))
      {
        curr_packet_len = (max_cur_packet_len - id_offset);
      }
      else
      {
        tx_data_buffer_int[id_offset + 3] |= 0x10;
        curr_packet_len = still_to_send;
      }
      get_trans_part(curr_data_position, curr_packet_len, &tx_data_buffer_int[id_offset + 5]);

      curr_data_position += curr_packet_len;

      /*END Copy the actual data into the TX buffer*/
    }
    else
    {
      Serial.println("Is only rx");
      ack_cont_data = 0;
      tx_data_buffer_int[id_offset + 3] = 0x80 | packet_counter_rx;
      if (display_more_data)
      {
        if (display_more_than_one)
        { //Display want another part of current data
          display_more_than_one = false;
          number_of_con_data = 0x01;
          tx_data_buffer_int[id_offset + 5] = 0x2B;
          packet_counter_rx = packet_counter_rx + 1;
          packet_counter_rx &= 0x0F;
        }
        else
        { //display wants to send more data
          tx_data_buffer_int[id_offset + 3] |= 0x10;
          display_more_data = false;
        }
      }
      else
      { //Last message sending now
        tx_data_buffer_int[id_offset + 5] = 0x2B;
        last_packet = true;
        set_rx_enable(false);
      }
    }
    tx_data_buffer_int[id_offset + 4] = number_of_con_data;

    cc1101_tx_fill(tx_data_buffer_int, (id_offset > 0) ? 61 : 62); //New activation is one byte shorter
    if (!multi_tx)
    {
      cc1101_tx();
    }

    Serial.print("PacketCounter: " + String(packet_counter));

    Serial.print(" rx: " + String(packet_counter_rx));
    if (number_of_con_data)
    {
      set_rx_enable(false);
      tx_state = 40;
      multi_tx = true;
      number_of_con_data--;
      packet_counter = packet_counter + 1;
      packet_counter &= 0x0F;
    }
    else
    {
      multi_tx = false;
    }

    print_buffer(tx_data_buffer_int, id_offset + 6 + 10 /*curr_packet_len*/);
  }

  void set_rx_enable(bool state)
  {
    next_rx_enable = state;
  }

  bool get_rx_enable()
  {
    bool temp_rx = next_rx_enable;
    next_rx_enable = false;
    return temp_rx;
  }

  bool check_ack(uint16_t ack_in, int counter, int len, int send_parts)
  {
    // Check if the received ACK contains all send counter packets, this could be made better/faster by skipping received parts on next part TX but is left out for now

    int temp_ack_size = send_parts + 1;
    if (temp_ack_size > 8)
      temp_ack_size = 8;

    int end_pos = counter + len + 1;
    if (end_pos > 16)
      end_pos -= 16;

    int start_pos = end_pos - temp_ack_size;
    if (start_pos < 0)
      start_pos = 16 + start_pos;

    uint16_t expected = 0x0000;

    for (int i = 0; i < 16; i++)
    {
      expected = expected << 1;
      if (end_pos > start_pos)
        expected |= ((i < end_pos) && (i >= start_pos)) ? 1 : 0;
      else
        expected |= ((i >= end_pos) && (i < start_pos)) ? 0 : 1;
    }

    printf("state cont data: %d curr counter: %d ack_size %d\r\n", counter, len, temp_ack_size);

    int bit = 0;
    printf("Expected ACK ");
    for (int i = 0; i < 16; i++)
    {
      bit = expected >> (15 - i) & 1;
      printf("%d", bit);
    }
    printf("\r\n");
    printf("Received ACK ");
    for (int i = 0; i < 16; i++)
    {
      bit = ack_in >> (15 - i) & 1;
      printf("%d", bit);
    }
    printf("\r\n");

    return expected == ack_in;
  }

  bool read_data_cc1101()
  {
    uint8_t data_array[255];
    int read_len = cc1101_read_fifo(data_array);

    if (read_len == -1)
    {
      Serial.println("Error while reading RX buffer");
      return false;
    }

    print_buffer(data_array, read_len);

    uint16_t ack_in = data_array[7 + id_offset] | (data_array[6 + id_offset] << 8);

    if (still_to_send && id_offset == 0 && !(check_ack(ack_in, last_position_to_go_back_counter, ack_cont_data, curr_data_position / max_cur_packet_len)))
    { //data was not fully received, will resend last parts
      curr_data_position = last_position_to_go_back;
      packet_counter = last_position_to_go_back_counter;
      timeout_counter++;
      check_timeout();
    }
    else
    {

      if (!(data_array[4 + id_offset] & 0x80))
      { //New actual data was received, adding it to the answer buffer.
        add_to_receive_buffer(&data_array[9 + ((id_offset > 0) ? 2 : 0)]);
      }
      display_more_data = !(data_array[4 + id_offset] & 0x10);     //Display wants more communication
      display_more_than_one = !(data_array[4 + id_offset] & 0xf0); //Display wants more than one packet
    }
    return true;
  }

  void check_timeout()
  {
    if (timeout_counter > 30)
    {
      if (get_trans_mode())
      {
        set_last_activation_status(2);
        set_trans_mode(0);
        restore_current_settings();
      }
      set_is_data_waiting(false);
      set_last_send_status(2);
      cc1101_idle();
      set_mode_idle();
    }
  }
};

ModeTrans modeTrans;
