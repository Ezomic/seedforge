#pragma once

#include "ichor/script/script_host.h"

namespace ichor {

class DataSet;

// Read-only access to the data layer:
//
//   ichor.data(table, id, field)   the value, or nil when any part is missing;
//                                  a list field comes back as one string joined with commas
//   ichor.data_has(table, id)      whether the record exists
//
// The set must outlive the host. Reloading it in place is safe, and the next
// call sees the new data.
void bindData(ScriptHost& host, const DataSet& data);

}
