pack:
	zip -r penncloud.zip . -x "*.o" "*.zip" "*~" "__pycache__/*" "*/__pycache__/*" "*.DS_Store" "*.out" "*.log"
