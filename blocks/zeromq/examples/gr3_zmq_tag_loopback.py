#!/usr/bin/env python3

import sys
import time

from gnuradio import blocks, gr, zeromq
import pmt


def make_tag(offset, key, value, srcid="gr3-loopback"):
    tag = gr.tag_t()
    tag.offset = offset
    tag.key = pmt.intern(key)
    tag.value = pmt.from_long(value)
    tag.srcid = pmt.intern(srcid)
    return tag


def main():
    in_endpoint = sys.argv[1] if len(sys.argv) > 1 else "tcp://127.0.0.1:5555"
    out_endpoint = sys.argv[2] if len(sys.argv) > 2 else "tcp://127.0.0.1:5556"

    data = [float(i) for i in range(32)]
    tags = [
        make_tag(0, "packet_len", 8),
        make_tag(0, "same_offset", 11),
        make_tag(5, "mid_burst", 22),
        make_tag(16, "next_packet", 8),
    ]

    recv_tb = gr.top_block("gr3 recv tagged zmq")
    pull = zeromq.pull_source(
        gr.sizeof_float, 1, out_endpoint, 100, True, -1, False
    )
    sink = blocks.vector_sink_f()
    tag_debug = blocks.tag_debug(gr.sizeof_float, "returned-tags")
    recv_tb.connect(pull, sink)
    recv_tb.connect(pull, tag_debug)

    send_tb = gr.top_block("gr3 send tagged zmq")
    src = blocks.vector_source_f(data, False, 1, tags)
    push = zeromq.push_sink(
        gr.sizeof_float, 1, in_endpoint, 100, True, -1, True
    )
    send_tb.connect(src, push)

    recv_tb.start()
    time.sleep(0.25)
    send_tb.run()
    time.sleep(1.0)
    recv_tb.stop()
    recv_tb.wait()

    received = list(sink.data())
    returned_tags = sink.tags()

    print(f"received {len(received)} samples")
    print(f"first samples: {received[:8]}")
    for tag in returned_tags:
        print(
            "tag",
            int(tag.offset),
            pmt.symbol_to_string(tag.key),
            pmt.to_python(tag.value),
            pmt.symbol_to_string(tag.srcid),
        )

    if received[: len(data)] != data:
        raise SystemExit("sample data did not round trip")

    keys = [pmt.symbol_to_string(tag.key) for tag in returned_tags]
    for expected in ("packet_len", "same_offset", "mid_burst", "next_packet"):
        if expected not in keys:
            raise SystemExit(f"missing returned tag {expected}")


if __name__ == "__main__":
    main()
