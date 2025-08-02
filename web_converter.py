#!/usr/bin/env python3
"""
Comprehensive Web Converter Tool
Converts between HTML/CSS/JS files and hex byte arrays
"""

import os
import sys
import re
from pathlib import Path

def file_to_hex_array(file_content):
    """Convert file content to hex byte array"""
    hex_bytes = []
    try:
        encoded_content = file_content.encode('utf-8')
    except UnicodeEncodeError:
        encoded_content = file_content.encode('ascii', errors='ignore')
    
    for byte in encoded_content:
        hex_bytes.append(f"0x{byte:02x}")
    
    return hex_bytes

def hex_to_string(hex_string):
    """Convert hex string to actual string"""
    try:
        hex_string = hex_string.replace('0x', '').replace(',', '').replace(' ', '')
        bytes_data = bytes.fromhex(hex_string)
        return bytes_data.decode('utf-8')
    except Exception as e:
        print(f"❌ Error converting hex: {e}")
        return None

def html_to_hex(web_folder, output_file="webfile.h"):
    """Convert HTML/CSS/JS files to hex header file"""
    print("🔄 Converting HTML/CSS/JS files to hex arrays...")
    
    web_folder = Path(web_folder)
    if not web_folder.exists():
        print(f"❌ Error: Web folder '{web_folder}' not found!")
        return False
    
    # Find all web files
    html_files = list(web_folder.glob('*.html'))
    css_files = list(web_folder.glob('*.css'))
    js_files = list(web_folder.glob('*.js'))
    
    all_files = html_files + css_files + js_files
    
    if not all_files:
        print(f"❌ No web files found in '{web_folder}'!")
        return False
    
    print(f"📁 Found {len(all_files)} files to convert...")
    
    # Process each file
    file_arrays = {}
    total_size = 0
    
    for file_path in all_files:
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            
            if not content.endswith('\n'):
                content += '\n'
            
            hex_array = file_to_hex_array(content)
            file_name = file_path.stem
            file_ext = file_path.suffix[1:]
            var_name = f"{file_name}_{file_ext}"
            
            file_arrays[var_name] = {
                'content': hex_array,
                'size': len(content),
                'path': str(file_path)
            }
            
            total_size += len(content)
            print(f"✅ Processed: {file_path.name} ({len(content)} bytes)")
            
        except Exception as e:
            print(f"❌ Error processing {file_path}: {e}")
            continue
    
    # Create header content
    header_content = f"""#ifndef WEBFILE_H
#define WEBFILE_H

// Auto-generated from web folder: {web_folder}
// Total files: {len(file_arrays)}
// Total size: {total_size} bytes

"""
    
    for var_name, file_info in file_arrays.items():
        header_content += f"""
// {file_info['path']} - {file_info['size']} bytes
const unsigned char {var_name}[] = {{
    {', '.join(file_info['content'])}
}};

const unsigned int {var_name}_len = sizeof({var_name});
"""
    
    header_content += f"""

// File count and total size
const unsigned int WEB_FILES_COUNT = {len(file_arrays)};
const unsigned int WEB_TOTAL_SIZE = {total_size};

#endif // WEBFILE_H
"""
    
    # Write header file
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(header_content)
        
        print(f"\n✅ Successfully created '{output_file}'")
        print(f"📊 Total files: {len(file_arrays)}")
        print(f"📊 Total size: {total_size} bytes")
        return True
        
    except Exception as e:
        print(f"❌ Error writing header file: {e}")
        return False

def hex_to_html(header_file, output_dir="converted_files"):
    """Convert hex arrays from header file back to HTML/CSS/JS files"""
    print("🔄 Converting hex arrays back to HTML/CSS/JS files...")
    
    # Create output directory
    output_path = Path(output_dir)
    output_path.mkdir(exist_ok=True)
    
    # Extract hex arrays
    arrays = {}
    try:
        with open(header_file, 'r', encoding='utf-8') as f:
            content = f.read()
        
        pattern = r'const unsigned char (\w+)\[\] = \{([^}]+)\};'
        matches = re.findall(pattern, content, re.DOTALL)
        
        for var_name, hex_content in matches:
            hex_string = re.sub(r'\s+', '', hex_content)
            arrays[var_name] = hex_string
            
        print(f"✅ Found {len(arrays)} hex arrays in {header_file}")
        
    except Exception as e:
        print(f"❌ Error reading header file: {e}")
        return False
    
    if not arrays:
        print("❌ No hex arrays found in header file!")
        return False
    
    success_count = 0
    
    for var_name, hex_string in arrays.items():
        try:
            file_content = hex_to_string(hex_string)
            
            if file_content is None:
                print(f"❌ Failed to convert {var_name}")
                continue
            
            # Determine file extension
            if 'html' in var_name.lower():
                extension = '.html'
            elif 'css' in var_name.lower():
                extension = '.css'
            elif 'js' in var_name.lower():
                extension = '.js'
            else:
                extension = '.txt'
            
            # Create filename
            filename = var_name.replace('_', '.') + extension
            file_path = output_path / filename
            
            # Write file
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(file_content)
            
            print(f"✅ Converted {var_name} -> {filename} ({len(file_content)} bytes)")
            success_count += 1
            
        except Exception as e:
            print(f"❌ Error converting {var_name}: {e}")
    
    print(f"\n📊 Conversion complete: {success_count}/{len(arrays)} files converted successfully!")
    print(f"📁 Files saved in: {output_path.absolute()}")
    
    return success_count > 0

def main():
    if len(sys.argv) < 2:
        print("Web Converter Tool")
        print("==================")
        print()
        print("Usage:")
        print("  python web_converter.py to-hex <web_folder> [output_file]")
        print("  python web_converter.py to-html <header_file> [output_dir]")
        print()
        print("Examples:")
        print("  python web_converter.py to-hex web")
        print("  python web_converter.py to-hex web webfile.h")
        print("  python web_converter.py to-html webfile.h")
        print("  python web_converter.py to-html webfile.h converted_files")
        return
    
    mode = sys.argv[1]
    
    if mode == "to-hex":
        if len(sys.argv) < 3:
            print("❌ Error: to-hex requires web folder path")
            return
        
        web_folder = sys.argv[2]
        output_file = sys.argv[3] if len(sys.argv) > 3 else "webfile.h"
        html_to_hex(web_folder, output_file)
        
    elif mode == "to-html":
        if len(sys.argv) < 3:
            print("❌ Error: to-html requires header file path")
            return
        
        header_file = sys.argv[2]
        output_dir = sys.argv[3] if len(sys.argv) > 3 else "converted_files"
        hex_to_html(header_file, output_dir)
        
    else:
        print(f"❌ Error: Unknown mode '{mode}'. Use 'to-hex' or 'to-html'")

if __name__ == "__main__":
    main() 