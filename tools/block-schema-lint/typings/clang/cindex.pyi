"""Minimal type stubs for clang.cindex C extension.

Only covers types and members used by scan_blocks.py.
The real module has extensive dynamic attribute generation.
"""

from typing import Any, List, Optional

class _Enum:
    pass

class CursorKind(_Enum):
    STRUCT_DECL: CursorKind = ...
    CLASS_DECL: CursorKind = ...
    CLASS_TEMPLATE: CursorKind = ...
    MACRO_INSTANTIATION: CursorKind = ...
    TYPE_ALIAS_DECL: CursorKind = ...
    FIELD_DECL: CursorKind = ...
    CXX_METHOD: CursorKind = ...
    CXX_BASE_SPECIFIER: CursorKind = ...

class SourceLocation:
    file: Any  # File object, str-like
    line: int
    column: int
    offset: int

class Diagnostic:
    severity: int
    spelling: str
    location: SourceLocation

class Token:
    spelling: str

class Type:
    spelling: str
    kind: CursorKind

    def get_canonical(self) -> Type: ...

class Cursor:
    spelling: str
    kind: CursorKind
    location: SourceLocation
    type: Type
    result_type: Type
    underlying_typedef_type: Type

    def get_children(self) -> List[Cursor]: ...
    def get_tokens(self) -> List[Token]: ...

class TranslationUnit:
    cursor: Cursor
    diagnostics: List[Diagnostic]
    spelling: str
    PARSE_DETAILED_PROCESSING_RECORD: int = ...

class Index:
    @staticmethod
    def create() -> Index: ...
    def parse(
        self,
        path: str,
        args: Optional[List[str]] = None,
        unsaved_files: Optional[List[Any]] = None,
        options: int = 0,
    ) -> TranslationUnit: ...

class Config:
    library_file: str

    @staticmethod
    def set_library_file(path: str) -> None: ...
    @staticmethod
    def set_library_path(path: str) -> None: ...
