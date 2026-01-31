#include "namegen.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
#include <random>

namespace namegen {

static const std::vector<std::string>& boy_first_names() {
    static const std::vector<std::string> v = {
        "Aaditya", "Aarav", "Aariv", "Aarush", "Aayush", "Abhinav", "Abeer", "Abhinav", "Adarsh", "Aditya",
        "Advait", "Agnivesh", "Ajay", "Ajitesh", "Akash", "Akshay", "Akarsh", "Alok", "Amar", "Amey",
        "Aman", "Anay", "Aniket", "Anish", "Anirudh", "Ankit", "Anmol", "Ansh", "Anshul", "Arav",
        "Arin", "Arjun", "Arnav", "Arvind", "Aryan", "Ashwin", "Atharv", "Atul", "Avik", "Avinash",
        "Avish", "Ayush", "Bhargav", "Bharat", "Bhavesh", "Bhavin", "Chaitanya", "Charan", "Chetan", "Chirag",
        "Chirayu", "Daksh", "Darsh", "Darshan", "Darvesh", "Deven", "Devansh", "Devraj", "Dharmesh", "Dhruv",
        "Dikshant", "Divit", "Divyesh", "Eklavya", "Eshan", "Eshaan", "Falgun", "Gatik", "Gauransh", "Gaurav",
        "Girish", "Gyan", "Hans", "Harivansh", "Harish", "Harit", "Harsh", "Harsha", "Harshad", "Harshit",
        "Harin", "Hitesh", "Hriday", "Ilesh", "Ishaan", "Ishank", "Ishir", "Ishwar", "Indrajit", "Ivaan",
        "Jai", "Jay", "Jaya", "Jayant", "Jayesh", "Jatin", "Jivraj", "Kairav", "Kamal", "Kanishk", "Kavin",
        "Kartik", "Kaushal", "Ketan", "Krish", "Krishiv", "Krishna", "Krupal", "Kunal", "Kushal", "Laksh",
        "Lakshya", "Lakshit", "Lalit", "Luv", "Madhav", "Madhur", "Mahesh", "Manan", "Manav", "Manish",
        "Mayank", "Mayur", "Mihir", "Mitul", "Moksh", "Mohit", "Naitik", "Nakul", "Naman", "Naren",
        "Nikhil", "Nikhilesh", "Nihal", "Nirek", "Nirav", "Nishant", "Ojas", "Om", "Omkar", "Oorjit",
        "Parikshit", "Parth", "Parthiv", "Pradyun", "Pranav", "Pranesh", "Pranay", "Pratham", "Pratik", "Pravin",
        "Prem", "Rachit", "Raghav", "Raj", "Rajan", "Rajesh", "Rajiv", "Rakesh", "Ram", "Raman",
        "Ramesh", "Ranan", "Ranbir", "Ranjan", "Ranjit", "Rashesh", "Ravish", "Reyansh", "Rishi", "Rishabh",
        "Rishit", "Ritvik", "Rohan", "Ronak", "Ronav", "Sagar", "Saket", "Sahil", "Samarth", "Samar",
        "Sameer", "Sandeep", "Sanjay", "Sanjit", "Sanket", "Sarvesh", "Saurabh", "Shaunak", "Shaurya", "Shaan",
        "Shailesh", "Shantanu", "Shrey", "Shreyas", "Shubham", "Siddhant", "Siddharth", "Soham", "Sohil",
        "Somesh", "Sparsh", "Subhash", "Sudarshan", "Sujal", "Sumeet", "Suraj", "Surya", "Suryansh", "Swapnil",
        "Tanay", "Tanvir", "Tanish", "Tanishq", "Taarush", "Tarun", "Tejas", "Trilok", "Tushar", "Uday",
        "Ujjwal", "Umesh", "Utkarsh", "Utsav", "Vaibhav", "Ved", "Vedant", "Vihan", "Vikram", "Vikrant",
        "Vimal", "Vinay", "Vinod", "Vipul", "Vishal", "Vishesh", "Vishnu", "Vatsal", "Yash", "Yashwant",
        "Yatin", "Yudhisthir", "Yug", "Yuvansh", "Yuvraj", "Zayan"};
    return v;
}

static const std::vector<std::string>& girl_first_names() {
    static const std::vector<std::string> v = {
        "Aadhya", "Aaradhya", "Aarohi", "Aarvi", "Aarya", "Aashvi", "Aayushi", "Abha", "Advika", "Aditi",
        "Akanksha", "Akshita", "Alisha", "Alpa", "Alka", "Amisha", "Anaya", "Anika", "Anshika", "Anvi", "Anvika", "Apoorva",
        "Arpita", "Arpita", "Ashita", "Avantika", "Bhavika", "Bhavini", "Bhavya", "Bhumika", "Bina", "Bhanvi",
        "Bhairavi", "Brinda", "Chahati", "Chaitali", "Chaitra", "Chandana", "Chandni", "Chandrika", "Charvi",
        "Chitrani", "Charmi", "Darika", "Darshika", "Darshana", "Damini", "Deepa", "Deepali", "Diya", "Divya",
        "Divisha", "Eesha", "Eeshani", "Ekta", "Ekaanshi", "Ela", "Esha", "Eshani", "Eshita", "Erisha",
        "Falak", "Falguni", "Farah", "Gargi", "Gauri", "Gitali", "Gayatri", "Grishma", "Harini",
        "Harishita", "Hema", "Heena", "Hiral", "Hiralika", "Himani", "Hridaya", "Ila", "Inaya", "Ipsita",
        "Ira", "Iravati", "Isha", "Ishita", "Ishika", "Ishani", "Ishwari", "Ishwarya", "Janvi", "Jagruti",
        "Jasleen", "Jaya", "Jayati", "Jhanvi", "Juhi", "Jivika", "Jyotsna", "Kajal", "Kalpana", "Kalyani",
        "Kanika", "Karishma", "Kashish", "Kavya", "Kavisha", "Keya", "Khushi", "Kimaya", "Kinjal", "Kirti",
        "Kriti", "Krupa", "Kshiti", "Laboni", "Lajita", "Lalita", "Lata", "Lavanya", "Lavina", "Lekha",
        "Lina", "Lisha", "Lohita", "Lopa", "Luvina", "Mahi", "Maahi", "Mahika", "Mahima", "Madhavi", "Maitri",
        "Mala", "Malini", "Manvi", "Manya", "Meera", "Mehek", "Minal", "Mitali", "Moksha", "Mridula",
        "Myra", "Naina", "Namrata", "Nandini", "Neha", "Nidhi", "Niharika", "Nila", "Nirali", "Nisha", "Nivriti",
        "Niyati", "Nishtha", "Ojasvi", "Oorja", "Oorvi", "Omisha", "Pallavi", "Paridhi", "Pari", "Parul", "Pankhuri",
        "Pooja", "Poojani", "Palak", "Pragnya", "Prachi", "Pranavi", "Pranjal", "Pranavi", "Prarthana", "Prerana",
        "Preeti", "Priya", "Priyanka", "Prisha", "Parineeta", "Rachna", "Rachita", "Radha", "Radhika",
        "Rajvi", "Ranya", "Rashi", "Reema", "Ridhima", "Riya", "Rupal", "Rupali", "Rutuja", "Saanvi",
        "Sakshi", "Sanchita", "Sanika", "Sanjana", "Sanya", "Sejal", "Shaila", "Shanaya", "Shalini",
        "Shambhavi", "Shanta", "Sharda", "Sharmila", "Shreya", "Sreya", "Shruti", "Shyla", "Simran",
        "Smita", "Sneha", "Sohini", "Sonal", "Sonali", "Suhani", "Sukanya", "Swara", "Tanisha", "Tanvi",
        "Tanirika", "Tarini", "Tara", "Tejal", "Trisha", "Tulika", "Tia", "Urvi", "Urvashi", "Uttara",
        "Vaidehi", "Vaishnavi", "Vanshika", "Vanya", "Varsha", "Varnika", "Vasudha", "Veda", "Vedika", "Vidhi", "Veena",
        "Vidhatri", "Vidya", "Vina", "Vinita", "Vishakha", "Vrinda", "Vritika", "Yami", "Yamini", "Yashasvi",
        "Yashika", "Yashvi", "Yashita", "Yuvika", "Zahra", "Zaina", "Zara", "Zarina", "Zeel", "Zeya", "Ziya", "Zoya"};
    return v;
}

static const std::vector<std::string>& surnames_list() {
    static const std::vector<std::string> v = {
        "Patel","Shah","Desai","Mehta","Trivedi","Joshi","Gandhi","Dave","Bhatt","Amin",
        "Vora","Thakkar","Sheth","Gohil","Shahani","Parmar","Solanki","Choksi","Modi","Talati",
        "Nagar","Barot","Chavda","Rathod","Bhayani","Zaveri","Kothari","Upadhyay","Mahida","Munot",
        "Sompura","Shukla","Goswami","Hathi","Bhart","Sanghvi","Kanani","Vaghani","Dholakia","Tank",
        "Parekh","Dalal","Mevawala","Patelwala","Dabhi","Chheda","Haria","Jani","Patelvi","Mandavia",
        "Acharya", "Adani", "Adhvaryu", "Ajmera", "Ambani", "Asher", "Bainsla", "Bapodra",
        "Bhagat", "Bhakta", "Bhansali", "Bhanwadia", "Bhuta", "Bhuva", "Bunha", "Chag", "Chandratre",
        "Chandratreya", "Chauhan", "Chikhalia", "Chinwalla", "Chitalia", "Chudasama", "Daftary",
        "Dhaduk", "Dhokia", "Dixit", "Dobariya", "Doshi", "Gaekwad", "Gajjar", "Ganatra", "Ganjawala",
        "Godhania", "Goradia", "Grigg", "Gupta", "Hathiwala", "Jadeja", "Jariwala", "Jobanputra",
        "Juthani", "Kachchhi", "Kagalwala", "Kakadia", "Kamdar", "Kanakia", "Kansagara", "Kansara", "Kapadia",
        "Karavadra", "Karia", "Kasana", "Katira", "Kotadia", "Kotak", "Kotecha", "Kuchhadia", "Kyada",
        "Lal", "Lalbhai", "Macwan", "Makavana", "Makwana", "Mankad", "Mankodi", "Mistry", "Modhwadia",
        "Mokani", "Mulani", "Munim", "Naik", "Nayak", "Odedara", "Odedra", "Oza", "Palan", "Panchal",
        "Pardava", "Parikh", "Pathak", "Pipalia", "Prajapati", "Purohit", "Sampat", "Sarabhai", "Savalia",
        "Servaia", "Shroff", "Sisodiya", "Somaiya", "Soni", "Sutaria", "Suthar", "Tandel", "Tanti",
        "Thakar", "Thanki", "Visaria", "Visariya", "Vyas", "Wala", "Zariwala", "Madani", "Malaviya", "Gaglani"};
    return v;
}

static std::mt19937 seeded_rng() {
    // Mix clock + random_device to avoid identical sequences on fast repeats.
    std::random_device rd;
    auto now = static_cast<unsigned>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::seed_seq seq{rd(), rd(), rd(), now, now ^ 0x9e3779b9U};
    return std::mt19937(seq);
}

static const std::vector<std::string>& all_full_names() {
    static const std::vector<std::string> all = []() {
        const auto& boys = boy_first_names();
        const auto& girls = girl_first_names();
        const auto& lasts = surnames_list();

        std::vector<std::string> out;
        out.reserve((boys.size() + girls.size()) * lasts.size());

        for (const auto& first : boys) {
            for (const auto& last : lasts) out.push_back(first + " " + last);
        }
        for (const auto& first : girls) {
            for (const auto& last : lasts) out.push_back(first + " " + last);
        }
        return out;
    }();
    return all;
}

int max_unique_count() {
    const auto& all = all_full_names();
    if (all.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(all.size());
}

size_t universe_size() {
    return all_full_names().size();
}

const std::string& universe_name_at(size_t idx) {
    return all_full_names().at(idx);
}

uint64_t universe_fingerprint() {
    // FNV-1a 64-bit over all bytes of all names.
    const uint64_t FNV_OFFSET = 1469598103934665603ULL;
    const uint64_t FNV_PRIME = 1099511628211ULL;
    uint64_t h = FNV_OFFSET;

    const auto& all = all_full_names();
    for (const auto& s : all) {
        for (unsigned char c : s) {
            h ^= static_cast<uint64_t>(c);
            h *= FNV_PRIME;
        }
        // Separator so ["ab","c"] != ["a","bc"]
        h ^= static_cast<uint64_t>(0xFF);
        h *= FNV_PRIME;
    }
    return h;
}

std::vector<std::string> generate_names(int count) {
    if (count <= 0 || count > kMaxCount) return {};

    const auto& all = all_full_names();
    if (count > static_cast<int>(all.size())) return {};

    auto rng = seeded_rng();

    std::vector<size_t> idx(all.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::shuffle(idx.begin(), idx.end(), rng);

    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) out.push_back(all[idx[static_cast<size_t>(i)]]);
    return out;
}

}  // namespace namegen

