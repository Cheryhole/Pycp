import argparse, os
from frontend import PycpParser, PycpInterpreter
import frontend.PycpAstNode as _nd

_compile_bytecode = False
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
	argparser.add_argument("-b", "--bytecode", action="store_true", help="生成字节码文件")

	args = argparser.parse_args()
	return args

if __name__ == "__main__":
	args = setup_argparser()
	_interpret = args.interpret
	_compile_bytecode = args.bytecode
	if _compile_bytecode:
		_ast = PycpParser.parsef(args.input_file)
		bc = _ast.as_bytecode()
		fn = args.output if args.output else os.path.basename(args.input_file).split(".")[0] + ".bpycp"
		with open(fn, "wb") as f:
			f.write(bc)

	if _interpret:
		if args.input_file.endswith(".bpycp"):
			with open(args.input_file, "rb") as f:
				source = f.read()
			_ast = _nd.from_bytecode(source)
		else:
			_ast = PycpParser.parsef(args.input_file)

		i = PycpInterpreter.Interpreter()
		i.run(_ast)
	#print(i.env)
