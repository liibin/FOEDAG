#Copyright 2021 The Foedag team

#GPL License

#Copyright (c) 2021 The Open-Source FPGA Foundation

#This program is free software: you can redistribute it and/or modify
#it under the terms of the GNU General Public License as published by
#the Free Software Foundation, either version 3 of the License, or
#(at your option) any later version.

#This program is distributed in the hope that it will be useful,
#but WITHOUT ANY WARRANTY; without even the implied warranty of
#MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#GNU General Public License for more details.

#You should have received a copy of the GNU General Public License
#along with this program.  If not, see <http://www.gnu.org/licenses/>.

proc add_des_file {} { 
gui_start
create_design asd
if { [catch {add_design_file asd.v} result] } {
    puts "PASSED: Caught negative test"
    puts $result
    exit 0
}
error "add_design_file expect failed"
exit 1
}