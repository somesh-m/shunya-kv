from .resp_client import send_resp_command

def test_cmd_set(server) -> None:
    response = send_resp_command(
        server.port, b"*3\r\n$3\r\nSET\r\n$4\r\nNAME\r\n$4\r\nTEST\r\n"
    )

    assert response == b"+OK\r\n"


def test_cmd_set_ttl(server) -> None:
    response = send_resp_command(
        server.port, b"*5\r\n$3\r\nSET\r\n$4\r\nNAME\r\n$4\r\nTEST\r\n$2\r\nEX\r\n$3\r\n300\r\n"
    )

    assert response == b"+OK\r\n"
