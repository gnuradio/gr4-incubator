#!/usr/bin/env python3

import argparse
import sys
import time

from gnuradio import blocks, gr, zeromq
import pmt


RAW_VALUES_INBOUND = [1.0, 2.0, 3.0, 4.0]
RAW_VALUES_OUTBOUND = [5.0, 6.0, 7.0, 8.0]
RAW_VALUES_TAGGED = [10.0, 20.0, 30.0, 40.0]


def make_nested_pair():
    return pmt.cons(pmt.from_long(1), pmt.cons(pmt.from_long(2), pmt.cons(pmt.from_long(3), pmt.PMT_NIL)))


def make_block():
    return gr.top_block()


def make_tag(offset, key, value, srcid=pmt.PMT_F):
    return gr.tag_utils.python_to_tag({"offset": offset, "key": key, "value": value, "srcid": srcid})


def collect_tags(tag_dbg):
    return list(tag_dbg.current_tags())


def wait_for(condition, timeout_s=5.0, step_s=0.01):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(step_s)
    return False


def raw_push_connect(endpoint):
    tb = make_block()
    src = blocks.vector_source_f(RAW_VALUES_INBOUND, False)
    sink = zeromq.push_sink(gr.sizeof_float, 1, endpoint, 100, False, -1, False)
    tb.connect(src, sink)
    time.sleep(0.15)
    tb.start()
    tb.wait()
    return 0


def raw_pull_connect(endpoint):
    tb = make_block()
    src = zeromq.pull_source(gr.sizeof_float, 1, endpoint, 100, False, -1, False)
    head = blocks.head(gr.sizeof_float, len(RAW_VALUES_OUTBOUND))
    sink = blocks.vector_sink_f()
    tb.connect(src, head, sink)
    time.sleep(0.3)
    tb.start()
    ok = wait_for(lambda: len(sink.data()) >= len(RAW_VALUES_OUTBOUND), timeout_s=3.0)
    tb.stop()
    tb.wait()
    if not ok:
        raise SystemExit(f"raw_pull_connect timeout: {list(sink.data())!r}")
    if list(sink.data()) != RAW_VALUES_OUTBOUND:
        raise SystemExit(f"raw_pull_connect mismatch: {list(sink.data())!r}")
    return 0


def raw_pub_connect(endpoint):
    tb = make_block()
    src = blocks.vector_source_f(RAW_VALUES_INBOUND, False)
    sink = zeromq.pub_sink(gr.sizeof_float, 1, endpoint, 100, False, -1, "", False, True)
    tb.connect(src, sink)
    time.sleep(0.15)
    tb.start()
    tb.wait()
    return 0


def raw_sub_connect(endpoint):
    tb = make_block()
    src = zeromq.sub_source(gr.sizeof_float, 1, endpoint, 100, False, -1, "", False)
    head = blocks.head(gr.sizeof_float, len(RAW_VALUES_OUTBOUND))
    sink = blocks.vector_sink_f()
    tb.connect(src, head, sink)
    time.sleep(0.3)
    tb.start()
    ok = wait_for(lambda: len(sink.data()) >= len(RAW_VALUES_OUTBOUND), timeout_s=3.0)
    tb.stop()
    tb.wait()
    if not ok:
        raise SystemExit(f"raw_sub_connect timeout: {list(sink.data())!r}")
    if list(sink.data()) != RAW_VALUES_OUTBOUND:
        raise SystemExit(f"raw_sub_connect mismatch: {list(sink.data())!r}")
    return 0


def raw_req_source(endpoint):
    tb = make_block()
    src = zeromq.req_source(gr.sizeof_float, 1, endpoint, 100, False, -1, False)
    head = blocks.head(gr.sizeof_float, len(RAW_VALUES_OUTBOUND))
    sink = blocks.vector_sink_f()
    tb.connect(src, head, sink)
    time.sleep(0.6)
    tb.start()
    ok = wait_for(lambda: len(sink.data()) >= len(RAW_VALUES_OUTBOUND), timeout_s=5.0)
    tb.stop()
    tb.wait()
    if not ok:
        raise SystemExit(f"raw_req_source timeout: {list(sink.data())!r}")
    if list(sink.data()) != RAW_VALUES_OUTBOUND:
        raise SystemExit(f"raw_req_source mismatch: {list(sink.data())!r}")
    return 0


def raw_rep_sink(endpoint):
    tb = make_block()
    src = blocks.vector_source_f(RAW_VALUES_OUTBOUND, True)
    head = blocks.head(gr.sizeof_float, len(RAW_VALUES_OUTBOUND))
    sink = zeromq.rep_sink(gr.sizeof_float, 1, endpoint, 100, False, -1, False)
    tb.connect(src, head, sink)
    time.sleep(0.15)
    tb.start()
    tb.wait()
    return 0


def pmt_push_connect(endpoint):
    tb = make_block()
    src = blocks.message_strobe(make_nested_pair(), 25)
    sink = zeromq.push_msg_sink(endpoint, 100, False)
    tb.msg_connect(src, "strobe", sink, "in")
    time.sleep(0.15)
    tb.start()
    time.sleep(0.45)
    tb.stop()
    tb.wait()
    return 0


def pmt_pull_connect(endpoint):
    tb = make_block()
    src = zeromq.pull_msg_source(endpoint, 100, False)
    dbg = blocks.message_debug()
    tb.msg_connect(src, "out", dbg, "store")
    time.sleep(0.5)
    tb.start()
    ok = wait_for(lambda: dbg.num_messages() >= 1, timeout_s=3.0)
    tb.stop()
    if not ok:
        raise SystemExit("timed out waiting for pmt message")
    got = dbg.get_message(0)
    expected = make_nested_pair()
    if not pmt.equal(got, expected):
        raise SystemExit(f"pmt_pull_connect mismatch: {pmt.write_string(got)}")
    return 0


def tagged_push_connect(endpoint):
    tb = make_block()
    src = blocks.vector_source_f(RAW_VALUES_TAGGED, False)
    tagged = blocks.stream_to_tagged_stream(gr.sizeof_float, 1, len(RAW_VALUES_TAGGED), "packet_len")
    sink = zeromq.push_sink(gr.sizeof_float, 1, endpoint, 100, True, -1, False)
    tb.connect(src, tagged, sink)
    time.sleep(0.15)
    tb.start()
    time.sleep(0.45)
    tb.stop()
    tb.wait()
    return 0


def tagged_pull_connect(endpoint):
    tb = make_block()
    src = zeromq.pull_source(gr.sizeof_float, 1, endpoint, 100, True, -1, False)
    head = blocks.head(gr.sizeof_float, len(RAW_VALUES_TAGGED))
    sink = blocks.vector_sink_f()
    tb.connect(src, head, sink)
    time.sleep(0.3)
    tb.start()
    ok = wait_for(lambda: len(sink.data()) >= len(RAW_VALUES_TAGGED), timeout_s=3.0)
    tb.stop()
    tb.wait()
    if not ok:
        raise SystemExit(f"tagged_pull_connect timeout: {list(sink.data())!r}")
    if list(sink.data()) != RAW_VALUES_TAGGED:
        raise SystemExit(f"tagged_pull_connect mismatch: {list(sink.data())!r}")
    return 0


def tagged_pull_multi_connect(endpoint):
    tb = make_block()
    src = zeromq.pull_source(gr.sizeof_float, 1, endpoint, 100, True, -1, False)
    head = blocks.head(gr.sizeof_float, 1)
    sink = blocks.vector_sink_f()
    tb.connect(src, head, sink)
    time.sleep(0.3)
    tb.start()
    ok = wait_for(lambda: len(sink.data()) >= 1, timeout_s=3.0)
    tb.stop()
    tb.wait()
    if not ok:
        raise SystemExit(f"tagged_pull_multi_connect timeout: {list(sink.data())!r}")
    if list(sink.data()) != [1.0]:
        raise SystemExit(f"tagged_pull_multi_connect mismatch: {list(sink.data())!r}")
    return 0


def probe():
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode")
    parser.add_argument("endpoint", nargs="?")
    args = parser.parse_args()

    modes = {
        "probe": probe,
        "raw_push_connect": raw_push_connect,
        "raw_pull_connect": raw_pull_connect,
        "raw_pub_connect": raw_pub_connect,
        "raw_sub_connect": raw_sub_connect,
        "raw_req_source": raw_req_source,
        "raw_rep_sink": raw_rep_sink,
        "pmt_push_connect": pmt_push_connect,
        "pmt_pull_connect": pmt_pull_connect,
        "tagged_push_connect": tagged_push_connect,
        "tagged_pull_connect": tagged_pull_connect,
        "tagged_pull_multi_connect": tagged_pull_multi_connect,
    }

    if args.mode not in modes:
        raise SystemExit(f"unknown mode: {args.mode}")
    if args.mode == "probe":
        return modes[args.mode]()
    if args.endpoint is None:
        raise SystemExit("endpoint is required")
    return modes[args.mode](args.endpoint)


if __name__ == "__main__":
    raise SystemExit(main())
