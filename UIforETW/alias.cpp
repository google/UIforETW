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

#include "stdafx.h"
#include "alias.h"


namespace debug {

//https://msdn.microsoft.com/en-us/library/chh3fb0k.aspx says:
//Using the optimize pragma with the empty string ("") is a special form of the directive:
//When you use the off parameter, it turns the optimizations, listed in the table earlier in this topic, off.
//When you use the on parameter, it resets the optimizations to those that you specified with the /O compiler option.
#pragma optimize("", off)

void Alias(const void* var)
{
	( void )var;
}

#pragma optimize("", on)

}