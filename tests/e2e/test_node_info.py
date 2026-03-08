from .resp_client import send_resp_command


def test_node_info_map_returns_bulk_json(server) -> None:
    response = send_resp_command(
        server.port, b"*2\r\n$9\r\nNODE_INFO\r\n$3\r\nMAP\r\n"
    )

    assert response.startswith(b"$")
    assert b"\"epoch\":1" in response
    assert b"\"smp\":" in response
    assert f"\"base_port\":{server.port}".encode() in response
    assert b"\"port_offset\":0" in response
    assert b"\"ranges\":[[" in response
  