#pragma once

// const float in a header file can lead to duplication of the storage
// but I don't really care in this case. Just don't do it with a header
// that is included hundreds of times.
const float kCurrentVersion = 1.42f;
