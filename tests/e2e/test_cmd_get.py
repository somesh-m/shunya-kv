import time

from .resp_client import send_resp_command

def test_cmd_get(server) -> None:
    response = send_resp_command(
        server.port, b"*3\r\n$3\r\nSET\r\n$4\r\nNAME\r\n$4\r\nTEST\r\n"
    )

    response = send_resp_command(
        server.port, b"*2\r\n$3\r\nGET\r\n$4\r\nNAME\r\n"
    )

    assert response == b"$4\r\nTEST\r\n"


def test_cmd_get_ttl(server) -> None:
    response = send_resp_command(
        server.port, b"*5\r\n$3\r\nSET\r\n$4\r\nNAME\r\n$4\r\nTEST\r\n$2\r\nEX\r\n$1\r\n5\r\n"
    )

    response = send_resp_command(
        server.port, b"*2\r\n$3\r\nGET\r\n$4\r\nNAME\r\n"
    )

    assert response == b"$4\r\nTEST\r\n"

    time.sleep(6)

    response = send_resp_command(
        server.port, b"*2\r\n$3\r\nGET\r\n$4\r\nNAME\r\n"
    )

    assert response == b"$-1\r\n"
