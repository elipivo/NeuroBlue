/*
 * BluetoothDriver.cpp
 *
 *  Created on: Aug 18, 2016
 *      Author: epivo
 */

#include <iostream>
#include <vector>
#include <array>
#include <string>

#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>

#include "../include/GestureDetector.h"

void sendByte(int client, uint8_t byte);
uint8_t readByte(int client);
void setup(Config& config);
int init_server();
sdp_session_t* register_service(uint8_t rfcomm_channel);

/**
 * Bluetooth packet label format
 */
enum label: uint8_t {
	null = 0x30, //!< null for meaningless packet (char 0)
	train,	//!< train packet label LABEL GESTURENAME (char 1)
	trainingDone, //!< training done packet label (char 2)
	detect, //!< detect packet label (char 3)
	stop //!< stop detecting packet label (char 4)
};

/**
 * This is a rapidly developed BluetoothDriver for the Onboard Gesture Detector
 * Demo. Lots of error checking has been skipped to get functionality finished.
 * Blocking issues also exist all over the place probably.
 * Shouldn't be considered finished and robust!
 *
 *
 * @return bleh
 */
int main() {

	//Setup the GestureDetector
	Config config;
	setup(config);
	GestureDetector gestureDetector{config};

	//start the Bluetooth server
	int manager = init_server();

	int recv = null;

	while (true) {

		/***********************
			  Wait for Byte
		*************************/
		do {
			recv = readByte(manager);
		} while (recv == null);


		/***********************
			Training Command
		*************************/

		if (recv == train) {

			std::cerr << "Awaiting Gesture Name..." << std::endl;

			// get gesture name byte
			do {
				recv = readByte(manager);
			} while (recv == null);

			std::cerr << "Training: " << std::to_string((int) recv) << "...";
			// train that gesture
			gestureDetector.train((int) recv);

			sendByte(manager, trainingDone);
			std::cerr << "done" << std::endl;

		}

		/***********************
			Detect Command
		*************************/

		else if (recv == detect) {

			std::cerr << "Detecting!" << std::endl;

			gestureDetector.start();

			while (true) {

				if (gestureDetector.gestureAvail()) {
					int name = gestureDetector.getGesture().getName();
					sendByte(manager, (uint8_t) name);
					std::cerr << "Detected: " << std::to_string(name) << std::endl;
				}

			}
		}

		else {
			std::cerr << "Invalid byte label received." << std::endl;
			std::cerr << "Got: " << std::hex << recv << std::endl;
		}
	}

	return 0;
}

void sendByte(int client, uint8_t byte) {
	write(client, (char*) &byte, sizeof(byte));
}

uint8_t readByte(int client) {
	uint8_t byte = null;
	read(client, (char*) &byte, sizeof(byte));
	return byte;
}

void setup(Config& config) {

	config.setCommandThreshold(.5);

	config.setClassifiers(std::vector<classifier_type>{SVM_t});
	config.setTrainingTime(6);

	config.setFeatures(std::vector<feature_type>{Sample_Variance_t});

	config.setStoreRawData(false);
	config.setStoreFeatures(false);

	config.setStoreGestures(true);
	config.setGesturesFilename("gesures.txt");

	config.setFeatureExtractionUpdateSize(100);
	config.setFeatureExtractionWindowSize(300);

	if (config.changed()) {
		config.changeAcknowledged();
	}

}

int init_server() {

	int port = 3, sock, client;
	struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
	char buffer[1024] = { 0 };
	socklen_t opt = sizeof(rem_addr);

	// local bluetooth adapter
	loc_addr.rc_family = AF_BLUETOOTH;
	bdaddr_t bdaddr_any = {0};
	loc_addr.rc_bdaddr = bdaddr_any; //*BDADDR_ANY;
	loc_addr.rc_channel = (uint8_t) port;

	// register service
	//sdp_session_t* session = register_service(port);
	register_service(port);

	// allocate socket
	sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

	// bind socket to port 3 of the first available
	 bind(sock, (struct sockaddr *)&loc_addr, sizeof(loc_addr));

	// put socket into listening mode
	listen(sock, 1);

	// accept one connection
	std::cerr << "Bluetooth Server Up." << std::endl;
	client = accept(sock, (struct sockaddr *)&rem_addr, &opt);
	std::cerr << "Client connected." << std::endl;

	ba2str(&rem_addr.rc_bdaddr, buffer);
	fprintf(stderr, "accepted connection from %s\n", buffer);
	memset(buffer, 0, sizeof(buffer));

	return client;
}

sdp_session_t *register_service(uint8_t rfcomm_channel) {

	/* A 128-bit number used to identify this service. The words are ordered from most to least
	* significant, but within each word, the octets are ordered from least to most significant.
	* For example, the UUID represneted by this array is 00001101-0000-1000-8000-00805F9B34FB. (The
	* hyphenation is a convention specified by the Service Discovery Protocol of the Bluetooth Core
	* Specification, but is not particularly important for this program.)
	*
	* This UUID is the Bluetooth Base UUID and is commonly used for simple Bluetooth applications.
	* Regardless of the UUID used, it must match the one that the Armatus Android app is searching
	* for.
	*/
	uint32_t svc_uuid_int[] = { 0x01110000, 0x00100000, 0x80000080, 0xFB349B5F };
	const char *service_name = "BioSleeve";
	const char *svc_dsc = "Communicate with the BioSleeve over Bluetooth";
	const char *service_prov = "JPL BioSleeve";

	uuid_t root_uuid, l2cap_uuid, rfcomm_uuid, svc_uuid,
	       svc_class_uuid;
	sdp_list_t *l2cap_list = 0,
	            *rfcomm_list = 0,
	             *root_list = 0,
	              *proto_list = 0,
	               *access_proto_list = 0,
	                *svc_class_list = 0,
	                 *profile_list = 0;
	sdp_data_t *channel = 0;
	sdp_profile_desc_t profile;
	sdp_record_t record = { 0 };
	sdp_session_t *session = 0;

	// set the general service ID
	sdp_uuid128_create(&svc_uuid, &svc_uuid_int);
	sdp_set_service_id(&record, svc_uuid);

	char str[256] = "";
	sdp_uuid2strn(&svc_uuid, str, 256);

	// set the service class
	sdp_uuid16_create(&svc_class_uuid, SERIAL_PORT_SVCLASS_ID);
	svc_class_list = sdp_list_append(0, &svc_class_uuid);
	sdp_set_service_classes(&record, svc_class_list);

	// set the Bluetooth profile information
	sdp_uuid16_create(&profile.uuid, SERIAL_PORT_PROFILE_ID);
	profile.version = 0x0100;
	profile_list = sdp_list_append(0, &profile);
	sdp_set_profile_descs(&record, profile_list);

	// make the service record publicly browsable
	sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
	root_list = sdp_list_append(0, &root_uuid);
	sdp_set_browse_groups(&record, root_list);

	// set l2cap information
	sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
	l2cap_list = sdp_list_append(0, &l2cap_uuid);
	proto_list = sdp_list_append(0, l2cap_list);

	// register the RFCOMM channel for RFCOMM sockets
	sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
	channel = sdp_data_alloc(SDP_UINT8, &rfcomm_channel);
	rfcomm_list = sdp_list_append(0, &rfcomm_uuid);
	sdp_list_append(rfcomm_list, channel);
	sdp_list_append(proto_list, rfcomm_list);

	access_proto_list = sdp_list_append(0, proto_list);
	sdp_set_access_protos(&record, access_proto_list);

	// set the name, provider, and description
	sdp_set_info_attr(&record, service_name, service_prov, svc_dsc);

	// connect to the local SDP server, register the service record,
	// and disconnect
	bdaddr_t bdaddr_any = {0};
	bdaddr_t bdaddr_local = {{0, 0, 0, 0xff, 0xff, 0xff}};
	session = sdp_connect(&bdaddr_any, &bdaddr_local, SDP_RETRY_IF_BUSY);
	sdp_record_register(session, &record, 0);

	// cleanup
	sdp_data_free(channel);
	sdp_list_free(l2cap_list, 0);
	sdp_list_free(rfcomm_list, 0);
	sdp_list_free(root_list, 0);
	sdp_list_free(access_proto_list, 0);
	sdp_list_free(svc_class_list, 0);
	sdp_list_free(profile_list, 0);

	return session;
}
