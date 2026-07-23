// SPDX-License-Identifier: ISC
// BlockCatalog — wraps a list of BlockDescriptors for fixture files.
//
// The scanner outputs a catalog JSON with version + source metadata.

package blockschema

// BlockCatalog is the top-level container for block catalog JSON.
BlockCatalog: {
	version: *1 | int
	source?: string | *""
	blocks: [...BlockDescriptor]
}
