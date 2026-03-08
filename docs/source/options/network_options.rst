Network options
---------------

--user_agent <string>

    Set a custom User-Agent string for HTTP requests.

--ca_file <file path>

    Absolute path to the certificate authority file for the server cert.
    PEM format. Optional, depends on server configuration.

--client_cert_file <file path>

    Absolute path to client certificate file. Optional, depends on server
    configuration.

--client_cert_private_key_file <file path>

    Absolute path to the private key file. Optional, depends on server
    configuration.

--client_cert_private_key_password <string>

    Password to the private key file. Optional, depends on server configuration.

--disable_peer_verification

    Disable peer verification. This is needed to talk to servers without valid
    certificates.

--udp_interface_address <ip_address>

    IP address of the interface over which to receive UDP unicast or multicast
    streams.
