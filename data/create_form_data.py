import json
import csv
from sys import argv


def infer_type(element):
    """Return (type_str, is_list) for a form input element."""
    t = element.get("type", "text")
    if t == "date":
        return ("date", False)
    if t == "radio":
        return ("string", False)
    if t == "checkbox":
        opts = element.get("opts", [])
        # treat as number list if every option parses as a number
        all_numeric = len(opts) > 0 and all(
            _is_numeric(o) for o in opts
        )
        return ("number" if all_numeric else "string", True)
    # text (and anything else)
    return ("string", False)


def _is_numeric(s):
    try:
        float(s)
        return True
    except (ValueError, TypeError):
        return False


def field_code(required_flag, type_str, is_list):
    """Return the two-character code for the .fields file second line."""
    req_char = required_flag   # 'a', 'o', or 'r' passed in directly
    type_initial = type_str[0]  # 's', 'n', or 'd'
    if is_list:
        type_initial = type_initial.upper()
    return req_char + type_initial


if __name__ == "__main__":
    fname = argv[1]
    json_path = f"www/forms/{fname}.json"
    csv_path = f"data/{fname}.csv"
    fields_path = f"data/{fname}.fields"

    with open(json_path) as f:
        form = json.load(f)

    # Collect only input elements that have a name
    inputs = [
        el for el in form.get("elements", [])
        if el.get("element") == "input" and "name" in el
    ]

    # Build field list: prepend the synthetic __time__ field
    fields = []

    # __time__: required date, marker 'a'
    fields.append({
        "name": "__time__",
        "type": "date",
        "is_list": False,
        "req_char": "a",
    })

    for el in inputs:
        type_str, is_list = infer_type(el)
        required = el.get("required", False)
        req_char = "r" if required else "o"
        fields.append({
            "name": el["name"],
            "type": type_str,
            "is_list": is_list,
            "req_char": req_char,
        })

    # Write CSV header
    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([field["name"] for field in fields])

    # Write .fields schema
    with open(fields_path, "w") as f:
        for field in fields:
            code = field_code(field["req_char"], field["type"], field["is_list"])
            f.write(field["name"] + "\n")
            f.write(code + "\n")
