// This sample code is intended to show that the Connext DDS Micro 2.4.12
// C API can be called from a C++ application. It is STRICTLY an example and
// NOT intended to represent production-quality code.

#include <stdio.h>

// headers from Connext DDS Micro installation
#include "rti_me_c.h"
#include "disc_dpse/disc_dpse_dpsediscovery.h"
#include "wh_sm/wh_sm_history.h"
#include "rh_sm/rh_sm_history.h"
#include "netio/netio_udp.h"

// rtiddsgen generated headers
#include "example.h"
#include "examplePlugin.h"
#include "exampleSupport.h"

// DPSE Discovery-related constants defined in this header
#include "discovery_constants.h"

void my_typeSubscriber_on_subscription_matched(
        void *listener_data,
        DDS_DataReader * reader,
        const struct DDS_SubscriptionMatchedStatus *status)
{
    if (status->current_count_change > 0) {
        printf("INFO: Matched a publisher\n");
    } else if (status->current_count_change < 0) {
        printf("INFO: Unmatched a publisher\n");
    }
}

void my_typeSubscriber_on_data_available(
        void *listener_data,
        DDS_DataReader * reader)
{
    my_typeDataReader *narrowed_reader = my_typeDataReader_narrow(reader);
    struct DDS_SampleInfoSeq info_seq = DDS_SEQUENCE_INITIALIZER;
    struct my_typeSeq sample_seq = DDS_SEQUENCE_INITIALIZER;
    const DDS_Long MAX_SAMPLES_PER_TAKE = 32;
    DDS_ReturnCode_t retcode;

    retcode = my_typeDataReader_take(
            narrowed_reader, 
            &sample_seq, 
            &info_seq, 
            MAX_SAMPLES_PER_TAKE, 
            DDS_ANY_SAMPLE_STATE, 
            DDS_ANY_VIEW_STATE, 
            DDS_ANY_INSTANCE_STATE);
    if (retcode != DDS_RETCODE_OK) {
        printf("ERROR: failed to take data, retcode = %d\n", retcode);
    }

    // print each valid sample taken
    DDS_Long i;
    for (i = 0; i < my_typeSeq_get_length(&sample_seq); ++i) {
        struct DDS_SampleInfo *sample_info = 
                DDS_SampleInfoSeq_get_reference(&info_seq, i);
        if (sample_info->valid_data) {
            my_type *sample = my_typeSeq_get_reference(&sample_seq, i);

            printf("\nValid sample received\n");
            printf("\tsample id = %d\n", sample->id);
            printf("\tsample msg = %s\n", sample->msg);
        } else {
            printf("\nSample received\n\tINVALID DATA\n");
        }
    }

    my_typeDataReader_return_loan(narrowed_reader, &sample_seq, &info_seq);
    my_typeSeq_finalize(&sample_seq);
    DDS_SampleInfoSeq_finalize(&info_seq);
}

int main(void)
{
    // user-configurable values
    char *peer = "127.0.0.1";
    char *loopback_name = "Loopback Pseudo-Interface 1";
    char *eth_nic_name = "Wireless LAN adapter Wi-Fi";
    int domain_id = 100;

    // System-wide and DomainParticipant related
    DDS_DomainParticipantFactory *dpf = NULL;
    struct DDS_DomainParticipantFactoryQos dpf_qos = 
            DDS_DomainParticipantFactoryQos_INITIALIZER;
    DDS_DomainParticipant *dp = NULL;
    struct DDS_DomainParticipantQos dp_qos = 
            DDS_DomainParticipantQos_INITIALIZER;
    struct DPSE_DiscoveryPluginProperty discovery_plugin_properties =
            DPSE_DiscoveryPluginProperty_INITIALIZER;
    RT_Registry_T *registry = NULL;
    struct UDP_InterfaceFactoryProperty *udp_property = NULL;
    DDS_Topic *topic = NULL;
    DDS_Entity *entity = NULL;

    // Publisher and DataWriter related
    DDS_Publisher *publisher = NULL;
    DDS_DataWriter *datawriter = NULL;
    struct DDS_DataWriterQos dw_qos = DDS_DataWriterQos_INITIALIZER;
    my_typeDataWriter *narrowed_datawriter = NULL;
    my_type *sample = NULL;
    struct DDS_SubscriptionBuiltinTopicData rem_subscription_data =
            DDS_SubscriptionBuiltinTopicData_INITIALIZER;
    int sample_count = 0;

    // Subscriber and DataReader related
    DDS_Subscriber *subscriber = NULL;
    DDS_DataReader *datareader = NULL;
    struct DDS_DataReaderQos dr_qos = DDS_DataReaderQos_INITIALIZER;
    struct DDS_DataReaderListener dr_listener = DDS_DataReaderListener_INITIALIZER;
    struct DDS_PublicationBuiltinTopicData rem_publication_data =
        DDS_PublicationBuiltinTopicData_INITIALIZER;

    DDS_ReturnCode_t retcode;
    DDS_Boolean success = DDS_BOOLEAN_FALSE;

    // create the DomainParticipantFactory and registry so that we can make some 
    // changes to the default values
    dpf = DDS_DomainParticipantFactory_get_instance();
    registry = DDS_DomainParticipantFactory_get_registry(dpf);

    // register writer history
    if (!RT_Registry_register(
            registry, 
            DDSHST_WRITER_DEFAULT_HISTORY_NAME,
            WHSM_HistoryFactory_get_interface(), 
            NULL, 
            NULL))
    {
        printf("ERROR: failed to register wh\n");
    }
    // register reader history
    if (!RT_Registry_register(
            registry, 
            DDSHST_READER_DEFAULT_HISTORY_NAME,
            RHSM_HistoryFactory_get_interface(), 
            NULL, 
            NULL))
    {
        printf("ERROR: failed to register rh\n");
    }

    // Set up the UDP transport's allowed interfaces. To do this we:
    // (1) unregister the UDP trasport
    // (2) name the allowed interfaces
    // (3) re-register the transport
    if(!RT_Registry_unregister(
            registry, 
            NETIO_DEFAULT_UDP_NAME, 
            NULL, 
            NULL)) 
    {
        printf("ERROR: failed to unregister udp\n");
    }

    udp_property = (struct UDP_InterfaceFactoryProperty *)
            malloc(sizeof(struct UDP_InterfaceFactoryProperty));
    if (udp_property == NULL) {
        printf("ERROR: failed to allocate udp properties\n");
    }
    *udp_property = UDP_INTERFACE_FACTORY_PROPERTY_DEFAULT;

    // For additional allowed interface(s), increase maximum and length, and
    // set interface below:
    REDA_StringSeq_set_maximum(&udp_property->allow_interface,2);
    REDA_StringSeq_set_length(&udp_property->allow_interface,2);
    *REDA_StringSeq_get_reference(&udp_property->allow_interface,0) = 
            DDS_String_dup(loopback_name); 
    *REDA_StringSeq_get_reference(&udp_property->allow_interface,1) = 
            DDS_String_dup(eth_nic_name); 

#if 0  

    // When you are working on an RTOS or other lightweight OS, the middleware
    // may not be able to get the NIC information automatically. In that case, 
    // the code below can be used to manually tell the middleware about an 
    // interface. The interface name ("en0" below) could be anything, but should
    // match what you included in the "allow_interface" calls above.

    if (!UDP_InterfaceTable_add_entry(
    		&udp_property->if_table,
            0xc0a864c8,	// IP address of 192.168.100.200
			0xffffff00, // netmask of 255.255.255.0
			"en0",
			UDP_INTERFACE_INTERFACE_UP_FLAG |
			UDP_INTERFACE_INTERFACE_MULTICAST_FLAG)) {

    	LOG(1, "failed to add interface")

    }
    
#endif

    if(!RT_Registry_register(
            registry, 
            NETIO_DEFAULT_UDP_NAME,
            UDP_InterfaceFactory_get_interface(),
            (struct RT_ComponentFactoryProperty*)udp_property, NULL))
    {
        printf("ERROR: failed to re-register udp\n");
    } 

    // register the dpse (discovery) component
    if (!RT_Registry_register(
            registry,
            "dpse",
            DPSE_DiscoveryFactory_get_interface(),
            &discovery_plugin_properties._parent, 
            NULL))
    {
        printf("ERROR: failed to register dpse\n");
    }

    // Now that we've finsihed the changes to the registry, we will start 
    // creating DDS entities. By setting autoenable_created_entities to false 
    // until all of the DDS entities are created, we limit all dynamic memory 
    // allocation to happen *before* the point where we enable everything.
    DDS_DomainParticipantFactory_get_qos(dpf, &dpf_qos);
    dpf_qos.entity_factory.autoenable_created_entities = DDS_BOOLEAN_FALSE;
    DDS_DomainParticipantFactory_set_qos(dpf, &dpf_qos);

    // configure discovery prior to creating our DomainParticipant
    if(!RT_ComponentFactoryId_set_name(&dp_qos.discovery.discovery.name, "dpse")) {
        printf("ERROR: failed to set discovery plugin name\n");
    }
    if(!DDS_StringSeq_set_maximum(&dp_qos.discovery.initial_peers, 1)) {
        printf("ERROR: failed to set initial peers maximum\n");
    }
    if (!DDS_StringSeq_set_length(&dp_qos.discovery.initial_peers, 1)) {
        printf("ERROR: failed to set initial peers length\n");
    }
    *DDS_StringSeq_get_reference(&dp_qos.discovery.initial_peers, 0) = 
            DDS_String_dup(peer);

    // configure the DomainParticipant's resource limits... these are just 
    // examples, if there are more remote or local endpoints these values would
    // need to be increased
    dp_qos.resource_limits.max_destination_ports = 4;
    dp_qos.resource_limits.max_receive_ports = 4;
    dp_qos.resource_limits.local_topic_allocation = 1;
    dp_qos.resource_limits.local_type_allocation = 1;
    dp_qos.resource_limits.local_reader_allocation = 1;
    dp_qos.resource_limits.local_writer_allocation = 1;
    dp_qos.resource_limits.remote_participant_allocation = 1;
    dp_qos.resource_limits.remote_reader_allocation = 8;
    dp_qos.resource_limits.remote_writer_allocation = 8;
    dp_qos.resource_limits.matching_reader_writer_pair_allocation = 4;
    dp_qos.resource_limits.matching_writer_reader_pair_allocation = 4;

    // set the name of the local DomainParticipant (i.e. - this application) 
    // from the constants defined in discovery_constants.h
    // (this is required for DPSE discovery)
    strcpy(dp_qos.participant_name.name, k_PARTICIANT02_NAME);

    // now the DomainParticipant can be created
    dp = DDS_DomainParticipantFactory_create_participant(
            dpf, 
            domain_id,
            &dp_qos, 
            NULL,
            DDS_STATUS_MASK_NONE);
    if(dp == NULL) {
        printf("ERROR: failed to create participant\n");
    }

    // register the type (my_type, from the idl) with the middleware
    retcode = DDS_DomainParticipant_register_type(
            dp,
            my_typeTypePlugin_get_default_type_name(),
            my_typeTypePlugin_get());
    if(retcode != DDS_RETCODE_OK) {
        printf("ERROR: failed to register type\n");
    }

    // Create the Topic to which we will subscribe. Note that the name of the 
    // Topic is stored in my_topic_name, which was defined in the IDL 
    topic = DDS_DomainParticipant_create_topic(
            dp,
            my_topic_name, // this constant is defined in the *.idl file
            my_typeTypePlugin_get_default_type_name(),
            &DDS_TOPIC_QOS_DEFAULT, 
            NULL,
            DDS_STATUS_MASK_NONE);
    if(topic == NULL) {
        printf("ERROR: topic == NULL\n");
    }

    // assert the remote DomainParticipant whos name is held in 
    // the constant k_PARTICIANT01_NAME, defined in discovery_constants.h
    retcode = DPSE_RemoteParticipant_assert(dp, k_PARTICIANT01_NAME);
    if(retcode != DDS_RETCODE_OK) {
        printf("ERROR: failed to assert remote participant\n");
    }

    //**************************************************************************
    // Local Publisher and DataWriter
    //**************************************************************************

    // create the Publisher
    publisher = DDS_DomainParticipant_create_publisher(
            dp,
            &DDS_PUBLISHER_QOS_DEFAULT,
            NULL,
            DDS_STATUS_MASK_NONE);
    if(publisher == NULL) {
        printf("ERROR: Publisher == NULL\n");
    }

    // Configure the DataWriter's QoS. Note that the 'rtps_object_id' that we 
    // assign to our own DataWriter here needs to be the same number the remote
    // DataReader will configure for its remote peer. We are defining these IDs
    // and other constants in discovery_constants.h
    dw_qos.protocol.rtps_object_id = k_OBJ_ID_PARTICIANT02_DW01;
    dw_qos.reliability.kind = DDS_RELIABLE_RELIABILITY_QOS;
    dw_qos.resource_limits.max_samples_per_instance = 1;
    dw_qos.resource_limits.max_instances = 2;
    dw_qos.resource_limits.max_samples = dw_qos.resource_limits.max_instances *
            dw_qos.resource_limits.max_samples_per_instance;
    dw_qos.history.depth = 1;
    dw_qos.protocol.rtps_reliable_writer.heartbeat_period.sec = 0;
    dw_qos.protocol.rtps_reliable_writer.heartbeat_period.nanosec = 250000000;

    // now create the DataWriter
    datawriter = DDS_Publisher_create_datawriter(
            publisher, 
            topic, 
            &dw_qos,
            NULL,
            DDS_STATUS_MASK_NONE);
    if(datawriter == NULL) {
        printf("ERROR: datawriter == NULL\n");
    } 

    // A DDS_DataWriter is not type-specific, thus we need to cast, or "narrow"
    // the DataWriter before we use it to write our samples
    narrowed_datawriter = my_typeDataWriter_narrow(datawriter);

    // create the data sample that we will write
    sample = my_type_create();
    if(sample == NULL) {
        printf("ERROR: failed my_type_create\n");
    }

    // When we use DPSE discovery we must mannually setup information about any 
    // DataReaders we are expecting to discover. This information includes a 
    // unique object ID for the remote peer (we are defining this in 
    // discovery_constants.h), as well as its Topic, type, and QoS. 
    rem_subscription_data.key.value[DDS_BUILTIN_TOPIC_KEY_OBJECT_ID] = 
            k_OBJ_ID_PARTICIANT01_DR01;
    rem_subscription_data.topic_name = DDS_String_dup(my_topic_name);
    rem_subscription_data.type_name = 
            DDS_String_dup(my_typeTypePlugin_get_default_type_name());
    rem_subscription_data.reliability.kind = DDS_RELIABLE_RELIABILITY_QOS;

    // Now we can assert that a remote DomainParticipant (with the name held in
    // k_PARTICIANT01_NAME) will contain a DataReader described by the 
    // information in the rem_subscription_data struct.
    retcode = DPSE_RemoteSubscription_assert(
            dp,
            k_PARTICIANT01_NAME,
            &rem_subscription_data,
            my_type_get_key_kind(my_typeTypePlugin_get(), NULL));
    if (retcode != DDS_RETCODE_OK) {
        printf("ERROR: failed to assert remote publication\n");
    }

    //**************************************************************************
    // Local Subscriber and DataReader
    //**************************************************************************

    // create the Subscriber
    subscriber = DDS_DomainParticipant_create_subscriber(
            dp,
            &DDS_SUBSCRIBER_QOS_DEFAULT,
            NULL, 
            DDS_STATUS_MASK_NONE);
    if(subscriber == NULL) {
        printf("ERROR: subscriber == NULL\n");
    }

    // Configure the listener callbacks. This listener will get passed to the 
    // DataReader when we create it
    dr_listener.on_data_available = my_typeSubscriber_on_data_available;
    dr_listener.on_subscription_matched = my_typeSubscriber_on_subscription_matched;

    // Configure the DataReader's QoS. Note that the 'rtps_object_id' that we 
    // assign to our own DataReader here needs to be the same number the remote
    // DataWriter will configure for its remote peer. We are defining these IDs
    // and other constants in discovery_constants.h
    dr_qos.protocol.rtps_object_id = k_OBJ_ID_PARTICIANT02_DR01;
    dr_qos.reliability.kind = DDS_RELIABLE_RELIABILITY_QOS;
    dr_qos.resource_limits.max_instances = 2;
    dr_qos.resource_limits.max_samples_per_instance = 16;
    dr_qos.resource_limits.max_samples = dr_qos.resource_limits.max_instances *
            dr_qos.resource_limits.max_samples_per_instance;
    dr_qos.reader_resource_limits.max_remote_writers = 1;
    dr_qos.reader_resource_limits.max_remote_writers_per_instance = 1;
    dr_qos.history.depth = 16;

    // create the DataReader
    datareader = DDS_Subscriber_create_datareader(
            subscriber,
            DDS_Topic_as_topicdescription(topic), 
            &dr_qos,
            &dr_listener,
            DDS_DATA_AVAILABLE_STATUS | DDS_SUBSCRIPTION_MATCHED_STATUS);
    if(datareader == NULL) {
        printf("ERROR: datareader == NULL\n");
    }

    // When we use DPSE discovery we must mannually setup information about any 
    // DataWriters we are expecting to discover. This information includes a 
    // unique object ID for the remote peer (we are defining this in 
    // discovery_constants.h), as well as its Topic, type, and QoS. 
    rem_publication_data.key.value[DDS_BUILTIN_TOPIC_KEY_OBJECT_ID] = 
            k_OBJ_ID_PARTICIANT01_DW01;
    rem_publication_data.topic_name = DDS_String_dup(my_topic_name);
    rem_publication_data.type_name = 
            DDS_String_dup(my_typeTypePlugin_get_default_type_name());
    rem_publication_data.reliability.kind = DDS_RELIABLE_RELIABILITY_QOS;    

    // Now we can assert that a remote DomainParticipant (with the name held in
    // k_PARTICIANT01_NAME) will contain a DataWriter described by the 
    // information in the rem_publication_data struct.
    retcode = DPSE_RemotePublication_assert(
            dp,
            k_PARTICIANT01_NAME,
            &rem_publication_data,
            my_type_get_key_kind(my_typeTypePlugin_get(), 
            NULL));
    if (retcode != DDS_RETCODE_OK) {
        printf("ERROR: failed to assert remote publication\n");
    }

    //**************************************************************************
    // DDS ENTITY CREATION COMPLETE
    //**************************************************************************

    // Finaly, now that all of the entities are created, we can enable them all
    entity = DDS_DomainParticipant_as_entity(dp);
    retcode = DDS_Entity_enable(entity);
    if(retcode != DDS_RETCODE_OK) {
        printf("ERROR: failed to enable entity\n");
    }

    while (1) {
        // add some data to the sample
        sample->id = 2; // remember that the "id" member is keyed
        sprintf(sample->msg, "Hello from cpuB #%d\n", sample_count);

        retcode = my_typeDataWriter_write(
                narrowed_datawriter, 
                sample, 
                &DDS_HANDLE_NIL);
        if(retcode != DDS_RETCODE_OK) {
            printf("ERROR: Failed to write sample\n");
        } else {
            printf("Wrote sample %d\n", sample_count); 
            sample_count++;
        } 
        OSAPI_Thread_sleep(1000); // sleep 1s between writes 
    }   
}

