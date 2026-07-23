// SPDX-License-Identifier: ISC
// BlockDescriptor — Cue schema matching scan_blocks.py JSON output.

package blockschema

BlockDescriptor: close({
	id:             string
	file:           string
	type_name?:     string
	summary?:       string
	template_params?: [...string]
	type_expansions?: [...[...string]]
	base_classes?:  [...string]

	// Ports
	inputs?:  [...PortDescriptor]
	outputs?: [...PortDescriptor]

	// Parameters
	parameters?: [...ParameterDescriptor]

	// Raw member classification (optional, full list from GR_MAKE_REFLECTABLE)
	members?: [...MemberDescriptor]
})

PortDescriptor: close({
	name:           string
	kind:           *"port" | string
	direction?:     *"input" | "output" | string
	type:           string
	original_type?: string
	cardinality_kind?: string
})

ParameterDescriptor: close({
	name:            string
	kind:            *"parameter" | string
	parameter_name?: string
	type:            string
	doc?:            string
	annotated_type?: string
	default?:         null | bool | number | string
	required?:       bool
	summary?:        string
})

// Members is the raw GR_MAKE_REFLECTABLE dump — field set varies per kind.
// Structural validation happens on inputs/outputs/parameters instead.
MemberDescriptor: {
	name: string
	kind: string
	type?:  string
	signature?: string
}
