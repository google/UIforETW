/*
Copyright 2015 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once

enum KeyLoggerState
{
	kKeyLoggerOff,
	kKeyLoggerAnonymized,
	kKeyLoggerFull
};

// Call SetKeyloggingState to set what type of key logging should
// be done. If the key logging thread is not started then it will
// be started. Call it with kKeyLoggerOff to shut down the key
// logging thread and stop key logging.
void SetKeyloggingState(KeyLoggerState) noexcept;
