#!/usr/bin/env python3
"""Focused regression tests for deferred option-file validation."""
import importlib.util
from pathlib import Path
import unittest


SPEC = importlib.util.spec_from_file_location("check_options", Path(__file__).with_name("check_options.py"))
check_options = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(check_options)


def source(after="", *, inside="output_filename = optarg;"):
    return f'''int main_fake() {{
    switch(c) {{
    case 'o':
        {inside}
        break;
    default:
        return 1;
    }}
{after}
}}
'''


def errors(text):
    return check_options.extract_switch_optarg(text)['o'].errors


class DeferredFileCheckTests(unittest.TestCase):
    def test_unconditional_and_guarded_same_variable_are_allowed(self):
        for after in (
            '    output_filename = ensure_writable(logger, output_filename);',
            '    if (!output_filename.empty()) {\n'
            '        output_filename = require_exists(logger, output_filename);\n'
            '    }',
        ):
            with self.subTest(after=after):
                self.assertEqual(errors(source(after)), [])

    def test_ordinary_immediate_guarded_option_behavior_is_unchanged(self):
        immediate = 'output_filename = ensure_writable(logger, optarg);'
        self.assertEqual(errors(source('', inside=immediate)), [])

    def test_missing_or_wrong_post_switch_checks_keep_diagnostic(self):
        cases = {
            'missing': '',
            'wrong_variable': '    other_filename = ensure_writable(logger, other_filename);',
            'unrelated_condition': '    if (enabled) {\n        output_filename = ensure_writable(logger, output_filename);\n    }',
            'wrong_guarded_variable': '    if (!other_filename.empty()) {\n        output_filename = ensure_writable(logger, output_filename);\n    }',
            'string_text': '    const char* text = "output_filename = ensure_writable(logger, output_filename);";',
            'other_function': '}\n\nint helper() {\n    output_filename = ensure_writable(logger, output_filename);',
            'other_function_commented_close': '} // main\n\nint helper() {\n    output_filename = ensure_writable(logger, output_filename);',
            'block_comment': '    /*\n    output_filename = ensure_writable(logger, output_filename);\n    */',
            'raw_string': '    const char* text = R"(\n    output_filename = ensure_writable(logger, output_filename);\n)";',
            'continued_line_comment': '    // ignored \\\n    output_filename = ensure_writable(logger, output_filename);',
            'inside_later_case': '',
        }
        for name, after in cases.items():
            text = source(after)
            if name == 'inside_later_case':
                text = source('', inside=(
                    'output_filename = optarg;\n        break;\n'
                    "    case 'x':\n        output_filename = ensure_writable(logger, output_filename);"))
            with self.subTest(case=name):
                self.assertEqual(len(errors(text)), 1)

    def test_bare_filename_suffixes_keep_diagnostic(self):
        for variable in sorted(check_options.FILENAME_VAR_ENDS):
            with self.subTest(variable=variable):
                self.assertEqual(len(errors(source(inside=f'{variable} = optarg;'))), 1)
                after = f'    {variable} = ensure_writable(logger, {variable});'
                self.assertEqual(errors(source(after, inside=f'{variable} = optarg;')), [])

    def test_same_variable_aliases_and_unrelated_diagnostics(self):
        inside = ('output_filename = optarg;\n        break;\n'
                  "    case 'a':\n        output_filename = optarg;\n        break;\n"
                  "    case 'x':\n        other_filename = optarg;")
        parsed = check_options.extract_switch_optarg(source(
            '    output_filename = ensure_writable(logger, output_filename);', inside=inside))
        self.assertEqual(parsed['o'].errors, [])
        self.assertEqual(parsed['a'].errors, [])
        self.assertEqual(len(parsed['x'].errors), 1)

    def test_check_before_assignment_is_not_a_deferred_validation(self):
        text = source('', inside=(
            'output_filename = ensure_writable(logger, output_filename);\n'
            '        output_filename = optarg;'))
        self.assertEqual(len(errors(text)), 1)


if __name__ == '__main__':
    unittest.main()
