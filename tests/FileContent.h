#pragma once

const std::istringstream visa_meta_text("\
Type;Text\n\
Rows;7\n\
Cols;2\n\
PAN;INT;8\n\
Risk_Score;INT;4\n\
");

const std::istringstream visa_csv("\
PAN;Risk_Score\n\
9970891536;99\n\
1632619219;97\n\
3273429032;92\n\
2490717994;100\n\
4567664634;100\n\
3136665904;90\n\
4953106736;100\n\
");