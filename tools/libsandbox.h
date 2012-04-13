/* Copyright 2009 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Julien Tinnes
 */
#ifndef LIBSANDBOX_H
#define LIBSANDBOX_H

#define SBX_D "SBX_D"
#define SBX_HELPER_PID "SBX_HELPER_PID"

#define MSG_CHROOTME 'C'
#define MSG_CHROOTED 'O'

pid_t chrootme();
int getdumpable();

#endif
