import argparse
from frontend import PycpParser, PycpInterpreter

_interpret = True

def setup_argparser() -> argparse.Namespace:
	argparser = argparse.ArgumentParser(
								description="Pycp Interpreter & Compiler", 
								add_help=False
							)

	argparser.add_argument("-h", "--help", action="help", help="显示帮助信息")
	argparser.add_argument("input_file", help="输入文件路径")
	argparser.add_argument("-o", "--output", help="输出文件路径")
	argparser.add_argument("-c", "--compile", action="store_false", dest="interpret", help="编译该程序")
	argparser.add_argument("-i", "--interpret", action="store_true", help="解释运行该程序")

	args = argparser.parse_args()
	return args

# 暂时Python实现后端
if __name__ == "__main__":
	args = setup_argparser()
	_interpret = args.interpret

	_ast = PycpParser.parsef(args.input_file)
	i = PycpInterpreter.Interpreter()
	i.run(_ast)
	#print(i.env)
