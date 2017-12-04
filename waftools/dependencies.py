import pprint
from waflib.Configure import conf
import deps_parser

pp = pprint.PrettyPrinter().pprint

def test_build(ctx):
    ctx(rule='echo hello', shell=True, always=True)

@conf
def parse_dependencies(ctx, checks):
    def valid_symbol(check_name):
        return not check_name.startswith('os-')

    def multicheck_dict(check):
        # our checks take ctx, dependency_id, we need to curry the
        # dependency_id in and just accept ctx as argument
        def curry_id(fn):
            def curried(ctx):
                return fn(ctx, check['name'])
            return curried

        result = {
            'id': check['name'],
            'msg': 'Checking for ' + check['desc'],
            'func': curry_id(check['func']),
            'mandatory': check.get('req', False),
        }

        if ('deps' in check):
            after_tests = deps_parser.symbols_list(check['deps'])
            result['after_tests'] = filter(valid_symbol, after_tests)


        return result

    known_deps = set()
    satisfied_deps = set()

    rules = map(multicheck_dict, checks)
    ctx.multicheck(*rules)

    ctx.env.known_deps = list(known_deps)
    ctx.env.satisfied_deps = list(satisfied_deps)

@conf
def dependency_satisfied(ctx, dependency_identifier):
    return False
