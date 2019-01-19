/* OpenHybrid - an open GRE tunnel bonding implemantion
 * Copyright (C) 2019  Friedrich Oslage <friedrich@oslage.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
bool send_grecpnotify_filterlistpackageack(uint8_t ackcode);
bool send_grecpnotify_tunnelverify();
bool send_grecpnotify_linkfailure(uint8_t tuntype);
bool send_grecpnotify_bypasstraffic(uint32_t kbit);
void handle_grecpnotify(uint8_t tuntype, void *attributes, int size);
