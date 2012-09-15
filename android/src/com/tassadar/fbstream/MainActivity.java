package com.tassadar.fbstream;

import java.io.IOException;
import java.io.OutputStreamWriter;

import android.app.Activity;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;
import android.os.Bundle;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.Window;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Spinner;

public class MainActivity extends Activity {

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_INDETERMINATE_PROGRESS);
        setContentView(R.layout.activity_main);

        m_binary = (EditText)findViewById(R.id.binary);
        m_ip = (EditText)findViewById(R.id.ip);
        m_port = (EditText)findViewById(R.id.port);
        m_compression = (Spinner)findViewById(R.id.compression);
        m_btn = (Button)findViewById(R.id.start);
        
        SharedPreferences cfg = getPreferences(MODE_PRIVATE);
        m_binary.setText(cfg.getString("binPath", "/sd-ext/fbstream"));
        m_ip.setText(cfg.getString("ip", "192.168.0.154"));
        m_port.setText(cfg.getString("port", "33334"));
        m_compression.setSelection(cfg.getInt("compression", 0));
        
        enableInput(false);
        
        checkRunning();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.activity_main, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case R.id.menu_check:
                checkRunning();
                return true;
            default:
                return super.onOptionsItemSelected(item);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        saveCfg();
    }

    private void saveCfg()
    {
        Editor cfg = getPreferences(MODE_PRIVATE).edit();
        cfg.putString("binPath", m_binary.getText().toString());
        cfg.putString("ip", m_ip.getText().toString());
        cfg.putString("port", m_port.getText().toString());
        cfg.putInt("compression", (int)m_compression.getSelectedItemId());
        cfg.commit();
    }

    private void enableInput(boolean enable)
    {
        m_binary.setEnabled(enable);
        m_ip.setEnabled(enable);
        m_port.setEnabled(enable);
        m_compression.setEnabled(enable);
        m_btn.setText(enable ? "Start" : "Stop");
    }

    private void checkRunning()
    {
        m_btn.setEnabled(false);
        setProgressBarIndeterminateVisibility(true);
        new Thread(new Runnable(){
           public void run()
           {
               final String pidof = MainActivity.runCmd("pidof fbstream");
               m_running = pidof.length() != 0;
               m_binary.post(new Runnable() {
                   public void run()
                   {
                       enableInput(!m_running);
                       setProgressBarIndeterminateVisibility(false);
                       m_btn.setEnabled(true);
                   }
               });
           }
        }).start();
    }
    
    public void start_onClick(View v)
    {
        m_btn.setEnabled(false);
        saveCfg();

        setProgressBarIndeterminateVisibility(true);

        new Thread(new Runnable(){
            public void run()
            {
                String pidof = MainActivity.runCmd("pidof fbstream");

                if(m_running != (pidof.length() != 0)) {
                    m_running = pidof.length() != 0;
                    m_binary.post(new Runnable() {
                        public void run()
                        {
                            enableInput(!m_running);
                            m_btn.setEnabled(true);
                            setProgressBarIndeterminateVisibility(false);
                        }
                    });
                    return;
                }

                String str = "";
                if(!m_running) {
                    String bin = m_binary.getText().toString();
                    if(!bin.endsWith("/fbstream"))
                        bin += "/fbstream";

                    str += "nohup ";
                    str += bin + " ";
                    str += m_ip.getText() + " ";
                    str += m_port.getText() + " ";
                    str += Integer.toString(m_compression.getSelectedItemPosition()+1);
                    str += " > /dev/null 2>&1 &";
                }
                else
                {
                    str += "kill -9 $(pidof fbstream)";
                }

                MainActivity.runCmd(str);

                pidof = MainActivity.runCmd("pidof fbstream");
                m_running = pidof.length() != 0;
                m_binary.post(new Runnable() {
                    public void run()
                    {
                        enableInput(!m_running);
                        m_btn.setEnabled(true);
                        setProgressBarIndeterminateVisibility(false);
                    }
                });
            }
         }).start();
    }

    private static final String runCmd(String command) {
        Process proc = null;
        OutputStreamWriter osw = null;
        StringBuilder sbstdOut = new StringBuilder();

        Log.i("fbstream", command);

        try {
            proc = Runtime.getRuntime().exec("su");
            osw = new OutputStreamWriter(proc.getOutputStream());
            osw.write(command);
            osw.flush();
            osw.close();
        } catch (IOException ex) {
            ex.printStackTrace();
            return null;
        } finally {
            if (osw != null) {
                try {
                    osw.close();
                } catch (IOException e) {
                    e.printStackTrace();
                    return null;
                }
            }
        }
        try {
            if (proc != null)
                proc.waitFor();
        } catch (InterruptedException e) {
            e.printStackTrace();
            return null;
        }

        int read;
        try {
            while((read = proc.getInputStream().read()) != -1)
                sbstdOut.append((char)read);
            while((read = proc.getErrorStream().read()) != -1)
                sbstdOut.append((char)read);
        } catch (IOException e) { }
        
        String res = sbstdOut.toString();

        // What the hell?!
        res = res.replace("FIX ME! implement ttyname() bionic/libc/bionic/stubs.c:360", "");

        if(res.equals("\n") || res.split("\n").length == 1)
            res = res.replaceAll("\n", "");
        proc.destroy();
        return res;
    }

    boolean m_running;
    EditText m_binary;
    EditText m_ip;
    EditText m_port;
    Spinner m_compression;
    Button m_btn;
}
