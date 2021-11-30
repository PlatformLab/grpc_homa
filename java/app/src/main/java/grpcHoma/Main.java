package grpcHoma;

import java.net.URL;
import java.net.URLClassLoader;

public class Main {
    public static void main(String[] args) {
        System.out.printf("Hello, world\n");
        System.out.printf("os.arch: %s, os.name: %s\n",
                System.getProperty("os.arch"), System.getProperty("os.name"));
        System.out.printf("Current directory: %s\n",
                System.getProperty("user.dir"));
        System.out.printf("Java class path: %s\n",
                System.getProperty("java.class.path"));

//        ClassLoader cl = ClassLoader.getSystemClassLoader();
//
//        URL[] urls = ((URLClassLoader)cl).getURLs();
//
//        for(URL url: urls){
//        	System.out.println(url.getFile());
//        }
        
        HomaSocket socket = new HomaSocket();
        
        System.out.printf("Created socket\n");
    }
}
