import json
import csv
import os
from sys import argv
from time import asctime


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

    dirpath = f"data/forms/{fname}"
    validation_dirpath = f"{dirpath}/validation"
    os.makedirs(dirpath, exist_ok=True)
    os.makedirs(validation_dirpath, exist_ok=True)

    json_path = f"www/forms/{fname}.json"
    csv_path = f"{dirpath}/results.csv"
    fields_path = f"{dirpath}/fields.txt"

    with open(json_path, "r") as f:
        form = json.load(f)

    # Collect only input elements that have a name
    inputs = [
        el for el in form.get("elements", [])
        if el.get("element") == "input" and "name" in el
    ]

    # Field list
    fields = []

    # __time__: required date, marker 'a'
    fields.append({
        "name": "__time__",
        "type": "date",
        "is_list": False,
        "req_char": "a",
    })

    # Iterate through elements
    for el in inputs:
        type_str, is_list = infer_type(el)
        name = el.get("name")
        required = el.get("required", False)
        validate = el.get("validate", False)
        req_char = "r" if required else "o"
        fields.append({
            "name": name,
            "type": type_str,
            "is_list": is_list,
            "req_char": req_char,
        })

        # add validation
        if validate or "opts" in el:
            validation_path = f"{validation_dirpath}/{name}.txt"
            with open(validation_path, "w") as f:
                f.write(f"# ENTER VALID VALUES FOR {name} BELOW THIS LINE\n")
                opts = el.get("opts", [])
                if len(opts) > 0:
                    for opt in opts:
                        if isinstance(opt, str):
                            f.write(opt + "\n")
                        else:
                            f.write(opt['text'] + "\n")

    if os.path.exists(csv_path):
        now = asctime().replace(" ", "_").replace(":", "_")
        backup_path = f"{csv_path}.{now}.csv"
        print(f"Moving {csv_path} to {backup_path}")
        os.rename(csv_path, backup_path)
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
