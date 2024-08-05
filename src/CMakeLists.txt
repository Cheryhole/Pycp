project(PycpLibrary)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

execute_process(COMMAND flex --outfile=PycpLex.cpp PycpLex.l)
execute_process(COMMAND bison --outfile=PycpParser.cpp --defines=PycpParser.h PycpParser.y)


set(Sources 
		)