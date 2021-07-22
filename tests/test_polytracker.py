from collections import defaultdict
import os
import pytest
from subprocess import CalledProcessError
from typing import Dict

from tqdm import tqdm

from polytracker import (
    BasicBlockEntry,
    FunctionEntry,
    FunctionReturn,
    ProgramTrace,
)

from .data import *


@pytest.mark.program_trace("test_mmap.c")
def test_source_mmap(program_trace: ProgramTrace):
    assert any(byte_offset.offset == 0 for byte_offset in program_trace.get_function("main").taints())


@pytest.mark.program_trace("test_open.c")
def test_source_open(program_trace: ProgramTrace):
    assert any(byte_offset.offset == 0 for byte_offset in program_trace.get_function("main").taints())


@pytest.mark.program_trace("test_control_flow.c")
def test_control_flow(program_trace: ProgramTrace):
    # make sure the trace contains all of the functions:
    main = program_trace.get_function("main")
    assert len(main.called_from()) == 0
    assert len(main.calls_to()) == 1
    func1 = program_trace.get_function("func1")
    assert func1 in main.calls_to()
    assert len(func1.called_from()) == 1
    assert main in func1.called_from()
    assert len(func1.calls_to()) == 1
    func2 = program_trace.get_function("func2")
    assert func2 in func1.calls_to()
    assert len(func2.called_from()) == 2
    assert func1 in func2.called_from()
    assert func2 in func2.called_from()
    assert len(func2.calls_to()) == 1
    entries: Dict[str, int] = defaultdict(int)
    returns: Dict[str, int] = defaultdict(int)
    for event in program_trace:
        if isinstance(event, BasicBlockEntry):
            assert event.entry_count() == 0
        elif isinstance(event, FunctionEntry):
            entries[event.function.name] += 1
        elif isinstance(event, FunctionReturn):
            returns[event.function.name] += 1
    assert entries["main"] == 1
    assert entries["func1"] == 1
    assert entries["func2"] == 6
    assert returns["func1"] == 1
    assert returns["func2"] == 6
    # check function invocation reconstruction:
    assert program_trace.entrypoint is not None
    assert program_trace.entrypoint.function == main
    assert not program_trace.entrypoint.touched_taint
    called_from_main = list(program_trace.entrypoint.calls())
    assert len(called_from_main) == 1
    assert called_from_main[0].function == func1
    assert not called_from_main[0].touched_taint
    called_from_func1 = list(called_from_main[0].calls())
    assert len(called_from_func1) == 1
    assert called_from_func1[0].function == func2
    assert not called_from_func1[0].touched_taint
    called_from_func2 = list(called_from_func1[0].calls())
    assert len(called_from_func2) == 1
    assert called_from_func2[0].function == func2
    assert not called_from_func2[0].touched_taint
    # our instrumentation doesn't currently emit a function return event for main, but that might change in the future
    # so for now just ignore main


# TODO: Update this test
# def test_polyprocess_taint_sets(json_path, forest_path):
#     logger.info("Testing taint set processing")
#     poly_proc = PolyProcess(json_path, forest_path)
#     poly_proc.process_taint_sets()
#     poly_proc.set_output_filepath("/tmp/polytracker.json")
#     poly_proc.output_processed_json()
#     assert os.path.exists("/tmp/polytracker.json") is True
#     with open("/tmp/polytracker.json", "r") as poly_json:
#         json_size = os.path.getsize("/tmp/polytracker.json")
#         polytracker_json = json.loads(poly_json.read(json_size))
#         if "tainted_functions" in poly_proc.polytracker_json:
#             assert "tainted_functions" in polytracker_json
#             for func in poly_proc.polytracker_json["tainted_functions"]:
#                 if "cmp_bytes" in poly_proc.polytracker_json["tainted_functions"][func]:
#                     assert "cmp_bytes" in polytracker_json["tainted_functions"][func]
#                 if "input_bytes" in poly_proc.polytracker_json["tainted_functions"][func]:
#                     assert "input_bytes" in polytracker_json["tainted_functions"][func]
#         assert "version" in polytracker_json
#         assert polytracker_json["version"] == poly_proc.polytracker_json["version"]
#         assert "runtime_cfg" in polytracker_json
#         assert len(polytracker_json["runtime_cfg"]["main"]) == 1
#         assert "taint_sources" in polytracker_json
#         assert "canonical_mapping" not in polytracker_json
#         assert "tainted_input_blocks" in polytracker_json


@pytest.mark.program_trace("test_open.c")
def test_source_open_full_validate_schema(program_trace: ProgramTrace):
    forest_path = os.path.join(TEST_RESULTS_DIR, "test_open.c0_forest.bin")
    json_path = os.path.join(TEST_RESULTS_DIR, "test_open.c0_process_set.json")
    assert any(byte_offset.offset == 0 for byte_offset in program_trace.get_function("main").taints())
    # TODO: Uncomment once we update this function
    # test_polyprocess_taint_sets(json_path, forest_path)


@pytest.mark.program_trace("test_memcpy.c")
def test_memcpy_propagate(program_trace: ProgramTrace):
    func = program_trace.get_function("touch_copied_byte")
    taints = func.taints()
    assert len(taints) == 1
    assert next(iter(taints)).offset == 0


@pytest.mark.program_trace("test_taint_log.c")
def test_taint_log(program_trace: ProgramTrace):
    taints = program_trace.get_function("main").taints()
    for i in range(0, 10):
        assert any(i == offset.offset for offset in taints)


@pytest.mark.program_trace("test_taint_log.c", config_path=CONFIG_DIR / "new_range.json")
def test_config_files(program_trace: ProgramTrace):
    # the new_range.json config changes the polystart/polyend to
    # POLYSTART: 1, POLYEND: 3
    taints = program_trace.get_function("main").taints()
    for i in range(1, 4):
        assert any(i == offset.offset for offset in taints)
    for i in range(4, 10):
        assert all(i != offset.offset for offset in taints)


@pytest.mark.program_trace("test_fopen.c")
def test_source_fopen(program_trace: ProgramTrace):
    taints = program_trace.get_function("main").taints()
    assert any(offset.offset == 0 for offset in taints)


@pytest.mark.program_trace("test_ifstream.cpp")
def test_source_ifstream(program_trace: ProgramTrace):
    taints = program_trace.get_function("main").taints()
    assert any(offset.offset == 0 for offset in taints)


@pytest.mark.program_trace("test_object_propagation.cpp")
def test_cxx_object_propagation(program_trace: ProgramTrace):
    for func in program_trace.functions:
        if func.demangled_name.startswith("tainted_string("):
            assert len(func.taints()) > 0


# TODO Compute DFG and query if we touch vector in libcxx from object
@pytest.mark.program_trace("test_vector.cpp")
def test_cxx_vector(program_trace: ProgramTrace):
    assert any(byte_offset.offset == 0 for byte_offset in program_trace.get_function("main").taints())


@pytest.mark.program_trace("test_fgetc.c", input="ABCDEFGH")
def test_fgetc(program_trace: ProgramTrace):
    for _ in program_trace:
        pass
    entrypoint = program_trace.entrypoint
    assert entrypoint is not None
    assert entrypoint.touched_taint
    tainted_regions = list(entrypoint.taints().regions())
    assert len(tainted_regions) == 1
    assert tainted_regions[0].value == b"ABCDEFGH"
    called_by_main = list(entrypoint.calls())
    assert len(called_by_main) == 8
    for i, called in enumerate(called_by_main):
        regions = list(called.taints().regions())
        assert len(regions) == 1
        assert regions[0].value == b"ABCDEFGH"[i : i + 1]


@pytest.mark.program_trace("test_simple_union.cpp", input="ABCDEFGH\n11235878\n")
def test_taint_forest(program_trace: ProgramTrace):
    had_taint_union = False
    for taint_node in tqdm(program_trace.taint_forest.nodes(), leave=False, desc="validating", unit=" taint nodes"):
        if taint_node.is_canonical():
            assert taint_node.parent_one is None
            assert taint_node.parent_two is None
        else:
            assert taint_node.parent_one is not None
            assert taint_node.parent_two is not None
            had_taint_union = True
    assert had_taint_union


@pytest.mark.program_trace("test_retcode.c", return_exceptions=True)
def test_retcode(program_trace: Union[ProgramTrace, Exception]):
    # test_retcode.c should cause a CalledProcessError to be returned since it has a non-zero exit code
    assert isinstance(program_trace, CalledProcessError)


@pytest.mark.program_trace("test_stack.c", return_exceptions=True)
def test_stack(program_trace: Union[ProgramTrace, Exception]):
    # test_retcode.c should cause a CalledProcessError to be returned since it has a non-zero exit code
    assert not isinstance(program_trace, CalledProcessError)
