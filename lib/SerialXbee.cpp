#include <iostream>
#include <memory>

#include "../include/SerialXbee.hpp"
#include "../include/ReceivePacket.hpp"
#include "../include/TransmitRequest.hpp"
#include "../include/Utility.hpp"
#include "../lib/Utility.cpp"

namespace XBEE {
	// TODO: Figure out why the below won't work for some dumb reason
	SerialXbee::SerialXbee() : m_io(), m_port(m_io) {
		Connect(kDefaultPath);
		ReadHandler = std::bind(&SerialXbee::PrintFrame, this, std::placeholders::_1);
		WriteHandler = std::bind(&SerialXbee::PrintFrame, this, std::placeholders::_1);
	}

	void SerialXbee::Connect(std::string device_path, uint32_t baud_rate) {
		m_port.open(device_path);
		m_port.set_option(boost::asio::serial_port_base::baud_rate(baud_rate));
		runner = boost::thread(boost::bind(&boost::asio::io_service::run, &m_io));
	}

	// TODO: Clean up and optimize this messy code when time permits
	// TODO: Implement checksum checking
	void SerialXbee::ParseFrame(const boost::system::error_code &error, size_t num_bytes) {
		using namespace boost::asio;

		char holder[8];
		uint16_t frame_length;
		uint8_t frame_type;
		Frame *cur_frame = NULL;

		if (error) {
			std::cout << "[ERROR] FOUND" << std::endl;
			// throw an error, by repeating system error code
		}
		
		/*if (num_bytes != 2) {
			std::cout << "[ERROR] NOT ENOUGH BYTES" << std::endl;
		}*/

		// Clears buffer (Should only contain 0x7E delimiter, which is added automatically when new Frame object is created)
		buffer.consume(num_bytes);
		num_bytes = 0;
		// Synchronously read the next 2 bytes of Frame (Frame Length)

		while (buffer.size() < 3)
			read(m_port, buffer, transfer_exactly(1));

		std::istream temp(&buffer);
		temp.get(holder[0]);
		temp.get(holder[1]);
		frame_length = (holder[0] << 8) + holder[1];
		temp.get(holder[0]);
		frame_type = holder[0];

		size_t read_amount = frame_length - buffer.size();
		read(m_port, buffer, transfer_exactly(read_amount));
		
		// Construct Frame object
		switch(FrameType(frame_type)) {
			case FrameType::RECEIVE_PACKET:
			{
				ReceivePacket frame;
				// Mac 64
				for (int i = 0; i < 8; i++)
					temp.get(holder[i]);

				frame.source_mac_64 =  (static_cast<uint64_t>(holder[0] & 0xFF) << 56);
				frame.source_mac_64 += (static_cast<uint64_t>(holder[1] & 0xFF) << 48);
				frame.source_mac_64 += (static_cast<uint64_t>(holder[2] & 0xFF) << 40);
				frame.source_mac_64 += (static_cast<uint64_t>(holder[3] & 0xFF) << 32);
				frame.source_mac_64 += (static_cast<uint64_t>(holder[4] & 0xFF) << 24);
				frame.source_mac_64 += (static_cast<uint64_t>(holder[5] & 0xFF) << 16);
				frame.source_mac_64 += (static_cast<uint64_t>(holder[6] & 0xFF) << 8);
				frame.source_mac_64 +=  static_cast<uint64_t>(holder[7]) & 0xFF;

				// Mac 16
				temp.get(holder[0]);
				temp.get(holder[1]);
				frame.source_mac_16 = (static_cast<uint64_t>(holder[0] & 0xFF) << 8);
				frame.source_mac_16 += static_cast<uint64_t>(holder[1]) & 0xFF;

				// Recieve Options
				temp.get(holder[0]);
				frame.options = holder[0];

				// Store into RecievePacket's data field
				uint8_t piece;
				for (int i = 0; i < frame_length - 13; i++)
					temp >> frame.data.at(i);
				frame.length = frame_length;
				temp.get(holder[0]);
				piece = holder[0];
				frame.checksum = piece;
				cur_frame = &frame;

				break;
			}
			default:
				// Throw unable to parse frame error
				break;
		}
		// TODO: Add support for user callback function with Frame pointer type
		ReadHandler(cur_frame);
		AsyncReadFrame();
	}

	void SerialXbee::FrameWritten(const boost::system::error_code &error, size_t num_bytes, Frame *a_frame) {
		using namespace boost::asio;

		if (error) {
			std::cout << "[ERROR] FOUND" << std::endl;
		}

		WriteHandler(a_frame);
	}

	void SerialXbee::PrintFrame(Frame *a_frame) {
		std::cout << a_frame->ToHexString(HexFormat::BYTE_SPACING) << std::endl;
	}

	void SerialXbee::AsyncReadFrame() {
		boost::asio::async_read_until(m_port, buffer, 0x7E, boost::bind(&SerialXbee::ParseFrame, this, _1, _2));
	}

	void SerialXbee::AsyncWriteFrame(Frame *a_frame) {
		std::vector<uint8_t> temp = a_frame->SerializeFrame();
		boost::asio::async_write(m_port, boost::asio::buffer(temp, temp.size()), boost::bind(&SerialXbee::FrameWritten, this, _1, _2, a_frame));
	}
}